#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <sstream>
#include <tuple>
#include <exception>

#include <functional>
#include <array>

#include <cstdarg>

namespace ArgPar {

//?==== most generic stringToType() you'll find out there ====?//

template <typename T>
std::string get_type_name(){
#if defined(__clang__)
	const std::string prefix = "[T = ";
	constexpr auto suffix = "]";
	const std::string function = __PRETTY_FUNCTION__;
#elif defined(__GNUC__)
	const std::string prefix = "with T = ";
	constexpr auto suffix = "; ";
	const std::string function = __PRETTY_FUNCTION__;
#elif defined(__MSC_VER)
	const std::string prefix = "get_type_name<";
	constexpr auto suffix = ">(void)";
	const std::string function = __FUNCSIG__;
#else
	return typeid(T).name; // return a normally mangled type if compiler does not support demangling
#endif
	
	const auto start = function.find(prefix) + prefix.size();
	const auto end = function.find(suffix);
	const auto size = end - start;
	return function.substr(start, size);
}

// checks if a type T has the >> operator, this works somehow? source: https://stackoverflow.com/a/18603716
template<class T, typename = decltype(std::declval<std::istream&>() >> std::declval<T&>() )>
std::true_type 	supports_stream_conversion_test(const T&);
std::false_type 	supports_stream_conversion_test(...);
template<class T> using supports_stream_conversion = decltype(supports_stream_conversion_test(std::declval<T>()));

// convert string to type T
template<typename T>
typename std::enable_if<supports_stream_conversion<T>::value, T>::type ToType( std::string& s ){
	std::stringstream convert(s);
	T value;
	convert >> value;
	if(convert.fail()){
		convert.clear();
		std::string dummy;
		convert >> dummy;
		const auto conc = "Cannot convert string to " + get_type_name<T>();
		throw std::invalid_argument(conc);
	}
	return value;
}

// SFINAE, catch class with no >> operator defined, throw error
template<typename T>
typename std::enable_if<!supports_stream_conversion<T>::value, T>::type ToType(std::string& s){
	// give a runtime error that the parameter string can not implicitly be converted to the given type T class
	// Assert is not used as the type of T is not properly passed
	throw std::invalid_argument("\n\nConversion from string to "+ get_type_name<T>()+" is not supported.\n"
						"Either add a custom parser function or extend istream with\n"
						"std::istream& operator>>(std::istream&, "+ get_type_name<T>()+"&){}\n");
	return T();
}

class ArgumentParser;

template<typename... ParamTypes>
class Argument {
	friend class ArgumentParser;

	static const unsigned char CalleeLengthBeforeDescription = 22;
	bool required = false;
	bool has_implicitValues = false;
	bool has_defaultValues = false;
	std::string calledwith = "";

	std::vector<std::string> Callees;
	std::string helpString;

	// Gets the type of the ParamTypes parameter pack at index I
	template<std::size_t I>
	using TupleTypeAt = typename std::tuple_element<I, std::tuple<ParamTypes...>>::type;

	std::tuple<ParamTypes...> _ParamTupleValues;
	std::tuple<ParamTypes...> _ParamTupleImplicitValues;
	std::array<std::string, sizeof...(ParamTypes)> _ParamTupleNames;

	const char *escape_table[14] = {
		"\\0", "1", "2", "3", "4", "5", "6", "\\a", "\\b", "\\t", "\\n", "\\v", "\\f", "\\r"
	};

	

	template<std::size_t I = 0>
	typename std::enable_if<I  < sizeof...(ParamTypes), void>::type FormatParameters(std::stringstream& ss) const{
		ss << "[" << _ParamTupleNames[I];
		if(has_defaultValues){
			ss << ": ";
			if(std::is_same<TupleTypeAt<I>, char>::value){
				char value = std::get<I>(_ParamTupleValues);
				if(value >= 32 && value <=126)
					ss << value;
				else if (value <= 13)
					ss << escape_table[value];
				else
					ss << (int)value;
			}
			else{
				ss << std::get<I>(_ParamTupleValues);
			}
		}
		ss << "]";
		FormatParameters<I+1>(ss);
	}

	template<std::size_t I = 0>
	typename std::enable_if<I == sizeof...(ParamTypes), void>::type FormatParameters(std::stringstream& ss) const {}

	std::string GetCalleeFormatted() const {
		std::stringstream calleeFormatted;
		for(int i = 0; i < Callees.size() - 1; i++){
			calleeFormatted << Callees[i] << ", ";
		}
		calleeFormatted << Callees.back();
		return calleeFormatted.str();
	}

	//?==== Argument parser logic ====?//
	// default argument parser, source: https://stackoverflow.com/a/6894436
	template<std::size_t I = 0>
	inline typename std::enable_if<I  < sizeof...(ParamTypes), void>::type _ParseArg(std::vector<std::string> Parameters){
		if(has_implicitValues && I == 0){
			_setTuple(_ParamTupleValues, _ParamTupleImplicitValues);
			return;
		}
		if(I >= Parameters.size()){
			std::stringstream exception_error;
			exception_error << "Not enough parameters for argument: " << calledwith << " default usage: \n";
			exception_error << *this;
			throw std::out_of_range(exception_error.str());
		}
		std::get<I>(_ParamTupleValues) = ToType<TupleTypeAt<I>>(Parameters[I]);
		_ParseArg<I+1>(Parameters);
	}

	// SFINAE
	template<std::size_t I = 0>
	inline typename std::enable_if<I == sizeof...(ParamTypes), void>::type _ParseArg(std::vector<std::string> Parameters){}

	//?==== handles details per parameter ====?//
	// Param detail setter
	template<std::size_t I = 0>
	inline typename std::enable_if<I  < sizeof...(ParamTypes), void>::type _setTuple(std::tuple<ParamTypes...>& dest, std::tuple<ParamTypes...>& src){
		std::get<I>(dest) = std::get<I>(src);
		_setTuple<I+1>(dest, src);
	}

	// SFINAE
	template<std::size_t I = 0>
	inline typename std::enable_if<I == sizeof...(ParamTypes), void>::type _setTuple(std::tuple<ParamTypes...>& dest, std::tuple<ParamTypes...>& src){}


	// Custom parser function wrapper
	std::function<void(Argument<ParamTypes...>& Arg, std::vector<std::string> Parameters)> _f_ParameterParser = nullptr;

	template<std::size_t I = 0>
	inline typename std::enable_if<I  < sizeof...(ParamTypes), void>::type InitParamNamesDefault(){
		_ParamTupleNames[I] = get_type_name<TupleTypeAt<I>>();
		InitParamNamesDefault<I+1>();
	}

	template<std::size_t I = 0>
	inline typename std::enable_if<I == sizeof...(ParamTypes), void>::type InitParamNamesDefault(){}

	void ParseArg(std::vector<std::string> Parameters){
		if(_f_ParameterParser)
			_f_ParameterParser(*this, Parameters);
		else
			_ParseArg(Parameters);
	}

public:
	template<std::size_t N>
	Argument(std::array<std::string, N> &&ArgNames) 
		:  Callees(ArgNames.begin(), ArgNames.end()){
		
		InitParamNamesDefault();

		std::sort(Callees.begin(), Callees.end(), [] 
		(const std::string& first, const std::string& second){
			return first.size() < second.size();
		});
	}

	~Argument() = default;

	Argument& ParameterNames(std::array<std::string, sizeof...(ParamTypes)> &&ParameterNames){
		for(int i = 0; i < sizeof...(ParamTypes); i++)
			_ParamTupleNames[i] = ParameterNames[i];
		return *this;
	}

	
	Argument& default_value(std::tuple<ParamTypes...> defaultValues){
		_setTuple(_ParamTupleValues, defaultValues);
		has_defaultValues = true;
		return *this;
	}

	Argument& implicit_value(std::tuple<ParamTypes...> implicitValues){
		_setTuple(_ParamTupleImplicitValues, implicitValues);
		has_implicitValues = true;
		return *this;
	}

	Argument& Help(std::string help) {helpString = help; return *this;}

	// getter / setter

	template<std::size_t I = 0>
	inline TupleTypeAt<I> param() const{return std::get<I>(_ParamTupleValues);}
	template<std::size_t I = 0>
	inline TupleTypeAt<I>& param(){return std::get<I>(_ParamTupleValues);}

	void setParserFunction(std::function<void(Argument<ParamTypes...>& Arg, std::vector<std::string> Parameters)> f_ParameterParser){
		_f_ParameterParser = f_ParameterParser;
	}


	// prints info about the argument
	friend std::ostream& operator<<(std::ostream& os, const Argument& ArgBase){
		std::string calleeFormatted = ArgBase.GetCalleeFormatted();
		os << "\t" << std::left << std::setw(CalleeLengthBeforeDescription) << calleeFormatted;
		if(calleeFormatted.length() >= CalleeLengthBeforeDescription)
			os << std::endl << "\t" << std::string(CalleeLengthBeforeDescription, ' ');
		os << ArgBase.helpString << std::endl;	
		os << "\tUsage: " << ArgBase.Callees[0];
		std::stringstream ss;
		ArgBase.FormatParameters(ss);
		os << " " << ss.str() << std::endl;
		return os;
	}
	// check if this argument is argument for this callee
	bool operator==(const std::string& callee) const {
		return std::find(Callees.begin(), Callees.end(), callee.c_str()) != Callees.end();
	}
};

class ArgumentParser {

};


}; // end of namespace

using namespace ArgPar;

// ProgramName --arg param1 ... paramX





/**
 * @brief Splits a string into multiple strings based on a delimiter
 * Delimiter is not returned 
 * @param source The string to be processed
 * @param delimiter The delimiter that is used for splitting the string around
 * @return std::vector<std::string> The split string
 */
std::vector<std::string> SplitByDelimiter(std::string source, std::string delimiter){
	std::vector<std::string> split;
	size_t pos = 0;
	while ((pos = source.find(delimiter)) != std::string::npos) {
		split.push_back(source.substr(0, pos));
		source.erase(0, pos + delimiter.length());
	}
	split.push_back(source);

	return split;
}

void parseSSHInput(Argument<std::string, std::string, std::string>& Arg, std::vector<std::string> Params){
	std::vector<std::string> sshlogin = SplitByDelimiter(Params[0], "@");
	Arg.param<0>() = sshlogin[0];	// save username
	Arg.param<1>() = sshlogin[1];	// save ip
	Arg.param<2>() =   Params[1];	// save password
}

int main(){
	// Argument<int> integerArgument;
	// Argument<std::string, std::string, std::string> sshUser;
	// myClassArgument.setParserFunction(parseMyClass);
	// Argument<int, double, char, unsigned int> _4ParamArgument({.description = "this is a description for the argument", .shortCallee = "-I", .longCallee = "--Int"});

	Argument<int, double, char, unsigned int> _4ParamArgument(
		std::array<std::string, 2>{"-I", "--Test"}
	);

	Argument<bool> Flag(
		std::array<std::string, 2>{"-F", "--Flag"}
	);

	Flag.ParameterNames({"boolean"});
	Flag.implicit_value(true);
	_4ParamArgument.Help("This is a help message");

	// _4ParamArgument.ParseArg({"-23", "0.2", "34", "8", "30"});

	// Flag.ParseArg({});

	// myClass MC;

	// Flag myClassFlag;
	// myClassFlag.setParserFunction(std::bind(parseFlagMyClass, std::ref(MC)));

	// Argument<> EmptyArg;

	// integerArgument.ParseArg({"1"});
	// sshUser.f_ParameterParse = parseSSHInput;
	// sshUser.ParseArg({"ai@192.168.8.1", "lindeni"});
	// myClassArgument.ParseArg({"1", "0.35", "hello!"});
	// myClassFlag.ParseArg();
	// _4ParamArgument.ParseArg({"1", "0.2", "h", "hello"});

	// std::cout << integerArgument[0] << std::endl;
	// std::cout << sshUser[0] << std::endl;
	// std::cout << sshUser[1] << std::endl;
	// std::cout << sshUser[2] << std::endl;
	// std::cout << myClassArgument.get<myClass>(0).param1 << std::endl;
	// std::cout << myClassArgument.get<myClass>(0).param2 << std::endl;
	// std::cout << myClassArgument.get<myClass>(0).param3 << std::endl;

	std::cout << _4ParamArgument << std::endl;
	std::cout << _4ParamArgument.param<0>() << std::endl;
	std::cout << _4ParamArgument.param<1>() << std::endl;
	std::cout << _4ParamArgument.param<2>() << std::endl;
	std::cout << _4ParamArgument.param<3>() << std::endl;

	std::cout << std::endl << Flag.param<0>() << std::endl;

	// std::cout << MC.param1 << std::endl;
	// std::cout << MC.param2 << std::endl;
	// std::cout << MC.param3 << std::endl;

	return 0;
}