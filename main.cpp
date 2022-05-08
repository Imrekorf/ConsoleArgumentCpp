#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <sstream>
#include <tuple>
#include <exception>

#include <functional>
#include <array>

struct ArgumentDetails {
	std::string shortCallee{};
	std::string longCallee{};
	std::string description{};
};

class _ArgumentBase {
protected:
	static const unsigned char CalleeLengthBeforeDescription = 22;
	bool required = false;
	std::string calledwith = "";

	ArgumentDetails _ArgDetails;

	virtual std::string GetCalleeFormatted() const {
		std::string calleeFormatted;
		if(_ArgDetails.shortCallee != "")
			calleeFormatted += _ArgDetails.shortCallee;
		if(_ArgDetails.longCallee != "")
			calleeFormatted += (calleeFormatted.length() ? ", " : "") + _ArgDetails.longCallee;
		return calleeFormatted;
	}

public:
	friend std::ostream& operator<<(std::ostream& os, const _ArgumentBase& ArgBase){
		std::string calleeFormatted = ArgBase.GetCalleeFormatted();
		os << "\t" << std::left << std::setw(CalleeLengthBeforeDescription) << calleeFormatted;
		if(calleeFormatted.length() >= CalleeLengthBeforeDescription)
			os << std::endl << "\t" << std::string(CalleeLengthBeforeDescription, ' ');
		os << ArgBase._ArgDetails.description << std::endl;	
		return os;
	}

	virtual void ParseArg(std::vector<std::string> Parameters) = 0;

	_ArgumentBase(const ArgumentDetails& ArgDet) : _ArgDetails(ArgDet) {};
	_ArgumentBase() {};

	void setDescription(std::string description) {_ArgDetails.description = description;}
	void setShortCallee(std::string shortCallee) {_ArgDetails.shortCallee = shortCallee;}
	void setLongCallee (std::string longCallee ) {_ArgDetails.longCallee  =  longCallee;}

	// Assign Argument details
	_ArgumentBase& operator=(const ArgumentDetails& ArgDet){
		if(ArgDet.description != "")
			_ArgDetails.description = ArgDet.description;
		if(ArgDet.shortCallee != "")
			_ArgDetails.shortCallee = ArgDet.shortCallee;
		if(ArgDet.longCallee  != "")
			_ArgDetails.longCallee  = ArgDet.longCallee;
		return *this;
	}

	// check if this argument is argument for this callee
	bool operator==(const std::string& callee) const {
		return (callee == _ArgDetails.shortCallee || callee == _ArgDetails.longCallee);
	}
};

template <typename T>
const std::string get_type_name()
{
#if defined(__clang__)
    const auto prefix = std::string("[T = ");
    const auto suffix = "]";
    const auto function = std::string(__PRETTY_FUNCTION__);
#elif defined(__GNUC__)
    const auto prefix = std::string("with T = ");
    const auto suffix = "; ";
    const auto function = std::string(__PRETTY_FUNCTION__);
#elif defined(__MSC_VER)
    const auto prefix = std::string("get_type_name<");
    const auto suffix = ">(void)";
    const auto function = std::string(__FUNCSIG__);
#else
	return typeid(T).name; // return a normally mangled type if compiler does not support demangling
#endif

    const auto start = function.find(prefix) + prefix.size();
    const auto end = function.find(suffix);
    const auto size = end - start;

    return function.substr(start, size);
}

template<typename T>
struct Parameter {
	T _value = T();
	std::string _description = "";
	std::string _name = "";
};

template<typename... ParamTypes>
class Argument : public _ArgumentBase {
	/**
	 * @brief Parameter class for argument-parameters
	 * 
	 * @tparam T the type of the parameter
	 */
	template<typename T>
	class _Parameter {
		Parameter<T> _P;
		void init(std::string name, const T& defaultValue, const std::string& description) {
			_P._name = name;
			_P._value = defaultValue;
			_P._description = description;
		}

	public:
		_Parameter(std::string name = "", const std::string& description = "", const T& defaultValue = T()){
			init(name, defaultValue, description);
		}
		_Parameter(std::string name, const std::string& description){
			init(name, T(), description);
		}

		T& value(){return _P._value;}
		T value() const {return _P._value;}

		std::string& description(){return _P._description;}
		std::string description() const {return _P._description;}
		std::string& name(){return _P._name;}
		std::string name() const {return _P._name;}

		_Parameter& operator=(const Parameter<T>& P){
			_P._name = P._name;
			_P._description = P._description;
			_P._value = P._value;
			return *this;
		}
	};


	std::tuple<_Parameter<ParamTypes>...> _ParamTuple;

	// Gets the type of the ParamTypes parameter pack at index I
	template<std::size_t I>
	using TupleTypeAt = typename std::tuple_element<I, std::tuple<ParamTypes...>>::type;

	//?==== most generic stringToType() you'll find out there ====?//
	// checks if a type T has the >> operator, this works somehow? source: https://stackoverflow.com/a/18603716
	template<class T, typename = decltype(std::declval<std::istream&>() >> std::declval<T&>() )>
	static std::true_type 	supports_stream_conversion_test(const T&);
	static std::false_type 	supports_stream_conversion_test(...);
	template<class T> using supports_stream_conversion = decltype(supports_stream_conversion_test(std::declval<T>()));

	// convert string to type T
	template<typename T>
	static inline typename std::enable_if<supports_stream_conversion<T>::value, T>::type ToType( std::string& s ){
		std::stringstream convert(s);
		T value;
		convert >> value;
		if(convert.fail()){
			convert.clear();
			std::string dummy;
			convert >> dummy;
			throw std::invalid_argument("Cannot convert string to " + get_type_name<T>());
		}
		return value;
	}

	// SFINAE, catch class with no >> operator defined, throw error
	template<typename T>
	static inline typename std::enable_if<!supports_stream_conversion<T>::value, T>::type ToType(std::string& s){
		// give a compile time error that the parameter string can not implicitly be converted to the given type T class
		throw std::invalid_argument("\n\nConversion from string to " + get_type_name<T>() + " is not supported.\n"
						 "Either add a custom parser function or extend istream with\n"
						 "std::istream& operator>>(std::istream&, " + get_type_name<T>() + "&){}\n");
		return T();
	}

	//?==== Argument parser logic ====?//
	// default argument parser, source: https://stackoverflow.com/a/6894436
	template<std::size_t I = 0>
	inline typename std::enable_if<I  < sizeof...(ParamTypes), void>::type _ParseArg(std::vector<std::string> Parameters){
		if(I >= Parameters.size()){
			std::stringstream exception_error;
			exception_error << "Not enough paramters for argument: " << calledwith << " default usage: \n";
			exception_error << *this;
			throw std::out_of_range(exception_error.str());
		}
		std::get<I>(_ParamTuple).value() = ToType<TupleTypeAt<I>>(Parameters[I]);
		_ParseArg<I+1>(Parameters);
	}

	// SFINAE
	template<std::size_t I = 0>
	inline typename std::enable_if<I == sizeof...(ParamTypes), void>::type _ParseArg(std::vector<std::string> Parameters){}

	// Custom parser function wrapper
	std::function<void(Argument<ParamTypes...>& Arg, std::vector<std::string> Parameters)> _f_ParameterParser = nullptr;

	//?==== handles details per parameter ====?//
	// Param detail setter
	template<std::size_t I = 0>
	inline typename std::enable_if<I  < sizeof...(ParamTypes), void>::type _setparam(std::tuple<Parameter<ParamTypes>...> ParamDetails){
		std::get<I>(_ParamTuple) = std::get<I>(ParamDetails);
		_setparam<I+1>(ParamDetails);
	}

	// SFINAE
	template<std::size_t I = 0>
	inline typename std::enable_if<I == sizeof...(ParamTypes), void>::type _setparam(std::tuple<Parameter<ParamTypes>...> ParamDetails){}


public:
	Argument(ArgumentDetails ArgDetails, std::tuple<Parameter<ParamTypes>...> ParamDetails) {
		_ArgDetails.shortCallee = ArgDetails.shortCallee;
		_ArgDetails.longCallee = ArgDetails.longCallee;
		_ArgDetails.description = ArgDetails.description;
		_setparam(ParamDetails);
	}

	void ParseArg(std::vector<std::string> Parameters){
		if(_f_ParameterParser)
			_f_ParameterParser(*this, Parameters);
		else
			_ParseArg(Parameters);
	}

	// getter / setter

	template<std::size_t I = 0>
	inline TupleTypeAt<I> param() const{
		return std::get<I>(_ParamTuple).value();
	}

	template<std::size_t I = 0>
	inline TupleTypeAt<I>& param(){
		return std::get<I>(_ParamTuple).value();
	}

	template<std::size_t I = 0>
	inline void setDescription(std::string desc){
		std::get<I>(_ParamTuple).description() = desc;
	}
	inline void setDescription(std::array<std::string, sizeof...(ParamTypes)> descriptions){
		_setDescriptions(descriptions); // call recursive foreach tuple handler
	}

	void setParserFunction(std::function<void(Argument<ParamTypes...>& Arg, std::vector<std::string> Parameters)> f_ParameterParser){
		_f_ParameterParser = f_ParameterParser;
	}
};


class Flag : public _ArgumentBase {
	bool state = 0;

	std::function<void()> _f_ParameterParser = nullptr;

	void ParseArg(std::vector<std::string> Parameters){
		if(_f_ParameterParser)
			_f_ParameterParser();
		// If this function is called it means that the flag was passed, so set state to 1
		state = 1; 
	}

public:
	Flag(void){}
	Flag(const ArgumentDetails& ArgDet) : _ArgumentBase(ArgDet) {};

	void ParseArg(){
		ParseArg({});
	}

	void setParserFunction(std::function<void()> f_ParameterParser){
		_f_ParameterParser = f_ParameterParser;
	}
};











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

// custom classes require an empty constructor
class myClass {
public:
	int param1;
	double param2;
	std::string param3;
};

void parseMyClass(Argument<myClass>& Arg, std::vector<std::string> Params){
	Arg.param<0>().param1 = std::stoi(Params[0]);	// save param1
	Arg.param<0>().param2 = std::stod(Params[1]);	// save param2
	Arg.param<0>().param3 = Params[2];				// save param3
}

void parseFlagMyClass(myClass& MC){
	MC.param1 = 25;
	MC.param2 = 2.3;
	MC.param3 = "flag set";
}


int main(){
	// Argument<int> integerArgument;
	// Argument<std::string, std::string, std::string> sshUser;
	// Argument<myClass> myClassArgument(parseMyClass);
	// Argument<int, double, char, unsigned int> _4ParamArgument({.description = "this is a description for the argument", .shortCallee = "-I", .longCallee = "--Int"});

	Argument<int, double, char, unsigned int> _4ParamArgument(
		{.shortCallee = "-I", .longCallee = "--Int", .description = "description goes here"},
		{
			{},
			{},
			{},
			{}
		});

	// myClass MC;

	// Flag myClassFlag;
	// myClassFlag.setParserFunction(std::bind(parseFlagMyClass, std::ref(MC)));

	// Argument<> EmptyArg;

	// integerArgument.ParseArg({"1"});
	// sshUser.f_ParameterParse = parseSSHInput;
	// sshUser.ParseArg({"ai@192.168.8.1", "lindeni"});
	// myClassArgument.ParseArg({"1", "0.35", "hello!"});
	// myClassFlag.ParseArg();
	_4ParamArgument.ParseArg({"1", "0.2", "h", "hello"});

	// std::cout << integerArgument[0] << std::endl;
	// std::cout << sshUser[0] << std::endl;
	// std::cout << sshUser[1] << std::endl;
	// std::cout << sshUser[2] << std::endl;
	// std::cout << myClassArgument.get<myClass>(0).param1 << std::endl;
	// std::cout << myClassArgument.get<myClass>(0).param2 << std::endl;
	// std::cout << myClassArgument.get<myClass>(0).param3 << std::endl;

	std::cout << _4ParamArgument.param<0>() << std::endl;
	std::cout << _4ParamArgument.param<1>() << std::endl;
	std::cout << _4ParamArgument.param<2>() << std::endl;
	std::cout << _4ParamArgument.param<3>() << std::endl;

	// std::cout << MC.param1 << std::endl;
	// std::cout << MC.param2 << std::endl;
	// std::cout << MC.param3 << std::endl;

	return 0;
}