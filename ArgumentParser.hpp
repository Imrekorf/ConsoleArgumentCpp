#include <iostream>
#include <string>
#include <cstring> // for strlen
#include <vector>
#include <iomanip>
#include <sstream>
#include <tuple>
#include <exception>
#include <map>

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
std::true_type 		supports_stream_conversion_test(const T&);
std::false_type 	supports_stream_conversion_test(...);
template<class T> using supports_stream_conversion = decltype(supports_stream_conversion_test(std::declval<T>()));

// convert string to type T
template<typename T>
const typename std::enable_if<supports_stream_conversion<T>::value, T>::type ToType(const std::string& s ) {
	if(s.empty()){
		throw std::invalid_argument("Conversion string is empty");
	}
	std::stringstream convert(s);
	T value;
	convert >> value;
	if(convert.fail()){
		convert.clear();
		std::string dummy;
		convert >> dummy;
		const auto conc = "Conversion from \"" + s + "\" to " + get_type_name<T>() + " failed";
		throw std::invalid_argument(conc);
	}
	return value;
}

// SFINAE, catch class with no >> operator defined, throw error
template<typename T>
const typename std::enable_if<!supports_stream_conversion<T>::value, T>::type ToType(const std::string& s) {
	// give a runtime error that the parameter string can not implicitly be converted to the given type T class
	// Assert is not used as the type of T is not properly passed
	throw std::invalid_argument("\n\nConversion from string to "+ get_type_name<T>()+" is not supported.\n"
						"Either add a custom parser function or extend istream with\n"
						"std::istream& operator>>(std::istream&, "+ get_type_name<T>()+"&){}\n");
	return T();
}

class ArgumentParser;

class Argument {
	friend class ArgumentParser;

	static const unsigned char CalleeLengthBeforeDescription = 22;
	bool required = false;
	bool has_implicitValues = false;
	bool has_defaultValues = false;
	bool is_flag = false;
	bool is_used = false;

	std::size_t _paramcount = 0;

	std::vector<std::string> Callees;
	std::string helpString = "Look at me, I forgot to add a help string!";

	std::vector<std::string> _ParamValues;
	std::vector<std::string> _ParamImplicitValues;
	std::vector<std::string> _ParamNames;

	std::function<void(const std::vector<std::string>&)> _f_ParameterParser = nullptr;
	std::function<void(const std::vector<std::string>&)> _f_ParameterParserValidator = nullptr;
	// Gets the type of the ParamTypes parameter pack at index I
	template<std::size_t I, typename ...ParamTypes>
	using TupleTypeAt = typename std::tuple_element<I, std::tuple<ParamTypes...>>::type;
	
	// Formats parameter names and default values if present
	void FormatParameters(std::stringstream& ss) const{
		if(is_flag)
			return;
		for(int i = 0; i < _ParamNames.size(); i++){
		ss << "[" << _ParamNames[i];
		if(has_defaultValues){
			ss << ": " << _ParamValues[i];
		}
		ss << "]";
		}
	}

	// Formats the Callee's
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
	void _ParseArg(std::vector<std::string>& Parameters){
		is_used = true;
		// If there is a validator, execute validator
		if(_f_ParameterParserValidator)
			_f_ParameterParserValidator(Parameters);
		// If there is a custom parser, execute that instead
		if(_f_ParameterParser)
			return _f_ParameterParser(Parameters);
		// If there are implicit values and no parameters given, use implicit values
		if(has_implicitValues && Parameters.size() == 0){
			for(int i = 0; i < _ParamValues.size(); i++)
				_ParamValues[i] = _ParamImplicitValues[i];
			return;
		}
		for(int i = 0; i < _ParamValues.size(); i++){
			try{
				_ParamValues[i] = Parameters[i];
			}
			catch(const std::exception& e){
				// out of range exception.
				if(has_defaultValues) // we have default values, no problem!.
					break;
				else{ // no default values and not enough parameters. Problem!
					std::stringstream exception_error;
					exception_error << "Not enough parameters for argument: " << Callees[0] << " default usage: \n\t";
					exception_error << GetCalleeFormatted() << " ";
					if(!has_implicitValues)	// don't print default values if there is an implicit value
						FormatParameters(exception_error);
					else
						exception_error << std::endl;
					throw std::out_of_range(exception_error.str());
				}
			}
		}
	}

	// Init parameter names based on variadic list, this creates default param names
	template<std::size_t I = 0, typename ...ParamTypes>
	inline typename std::enable_if<I  < sizeof...(ParamTypes), void>::type InitParamNamesDefault(){
		_ParamNames[I] = get_type_name<TupleTypeAt<I, ParamTypes...>>();
		InitParamNamesDefault<I+1, ParamTypes...>();
	}
	// SFINEA
	template<std::size_t I = 0, typename ...ParamTypes>
	inline typename std::enable_if<I == sizeof...(ParamTypes), void>::type InitParamNamesDefault(){}

	// Init implicit values based on variadic list
	template<std::size_t I = 0, typename ...ParamTypes>
	typename std::enable_if<I  < sizeof...(ParamTypes), void>::type implicit_value(std::tuple<ParamTypes...> t){
		_ParamImplicitValues[I] = std::to_string(std::get<I>(t));
		implicit_value<I+1, ParamTypes...>(t);
	}
	//SFINEA
	template<std::size_t I = 0, typename ...ParamTypes>
	typename std::enable_if<I == sizeof...(ParamTypes), void>::type implicit_value(std::tuple<ParamTypes...> t){}

	// Init default values based on variadic list
	template<std::size_t I = 0, typename ...ParamTypes>
	typename std::enable_if<I  < sizeof...(ParamTypes), void>::type default_value(std::tuple<ParamTypes...> t){
		_ParamValues[I] = std::to_string(std::get<I>(t));
		default_value<I+1, ParamTypes...>(t);
	}
	//SFINEA
	template<std::size_t I = 0, typename ...ParamTypes>
	typename std::enable_if<I == sizeof...(ParamTypes), void>::type default_value(std::tuple<ParamTypes...> t){}

	// Init parameter names based on variadic list
	template<std::size_t I = 0, typename ...ParamTypes>
	typename std::enable_if<I  < sizeof...(ParamTypes), void>::type parameter_name(std::tuple<ParamTypes...> t){
		_ParamNames[I] = std::get<I>(t);
		parameter_name<I+1, ParamTypes...>(t);
	}
	//SFINEA
	template<std::size_t I = 0, typename ...ParamTypes>
	typename std::enable_if<I == sizeof...(ParamTypes), void>::type parameter_name(std::tuple<ParamTypes...> t){}

public:
	
	/**
	 * @brief Construct a new Argument object
	 * 
	 * @param paramcount The amount of parameters this argument will use
	 * @param ArgName First Possible Argument Callee, Single character prefix with -, multi character prefix with --
	 * @param ArgName2 Second Possible Argument Callee, Single character prefix with -, multi character prefix with --
	 */
	Argument(std::size_t paramcount, std::string ArgName, std::string ArgName2 = "") 
		: _paramcount(paramcount), Callees(1), _ParamValues(paramcount), _ParamImplicitValues(paramcount), _ParamNames(paramcount){
		
		Callees[0] = ArgName;
		if(!ArgName2.empty())
			Callees.push_back(ArgName2);
	}

	/**
	 * @brief Copy operator
	 * 
	 * @param A The argument to copy from
	 * @return Argument& The argument reference
	 */
	Argument& operator=(const Argument& A) {
		required = A.required;
		has_implicitValues = A.has_implicitValues;
		has_defaultValues = A.has_defaultValues;
		is_flag = A.is_flag;
		std::copy(A.Callees.begin(), A.Callees.end(), Callees.begin());
		helpString = A.helpString;
		std::copy(A._ParamValues.begin(), A._ParamValues.end(), _ParamValues.begin());
		std::copy(A._ParamImplicitValues.begin(), A._ParamImplicitValues.end(), _ParamImplicitValues.begin());
		std::copy(A._ParamNames.begin(), A._ParamNames.end(), _ParamNames.begin());
		_f_ParameterParser = A._f_ParameterParser;
		_f_ParameterParserValidator = A._f_ParameterParserValidator;
		return *this;
	}

	/**
	 * @brief Sets the name per parameter
	 * 
	 * @tparam ParamTypes list of parameter types
	 * @param ParameterNames String names of the parameters
	 * @return Argument& The argument reference
	 */
	template<typename ...ParamTypes>
	Argument& ParameterName(ParamTypes... ParameterNames){
		parameter_name<0, ParamTypes...>(std::tuple<ParamTypes...>(ParameterNames...));
		return *this;
	}

	/**
	 * @brief Sets the default value per paramater
	 * Default values for the argument. If the argument is not passed these values are used.
	 * @tparam ParamTypes list of parameter types
	 * @param defaultValues Default value per parameter
	 * @return Argument& The argument reference
	 */
	template<typename ...ParamTypes>
	Argument& defaultValue(ParamTypes... defaultValues){
		default_value<0, ParamTypes...>(std::tuple<ParamTypes...>(defaultValues...));
		has_defaultValues = true;
		return *this;
	}

	/**
	 * @brief Sets the implicit value per parameter
	 * Implicit values are values used if the argument is passed but no parameters are passed
	 * @tparam ParamTypes list of parameter types
	 * @param implicitValues Implicit value per parameter
	 * @return Argument& The argument reference
	 */
	template<typename ...ParamTypes>
	Argument& implicitValue(ParamTypes... implicitValues){
		implicit_value<0, ParamTypes...>(std::tuple<ParamTypes...>(implicitValues...));
		has_implicitValues = true;
		return *this;
	}

	/**
	 * @brief Sets the help message for the argument
	 * 
	 * @param help the help message string
	 * @return Argument& The argument reference
	 */
	Argument& Help(std::string help) {helpString = help; return *this;}
	/**
	 * @brief Sets the argument as required
	 * If the argument is required but not passed an error is thrown during parsing
	 * @return Argument& The argument reference
	 */
	Argument& Required(){required = true; return *this;}

	/**
	 * @brief Sets a custom argument parser function. 
	 * Gets called when the argument is being parsed instead of the default parser. Has access to the parameter vector that would normally be parsed for the argument.
	 * @param Parser The function to the custom argument parser
	 * @return Argument& The argument reference
	 */
	Argument& action(std::function<void(const std::vector<std::string>&)> Parser){
		_f_ParameterParser = Parser;
		return *this;
	}

	/**
	 * @brief Gets a string value reference of the parameter based on idx
	 * 
	 * @param idx The position of the parameter in the list
	 * @return std::string& A reference to the parameter string value
	 */
	std::string& operator[](std::size_t idx) {return _ParamValues[idx];}
	/**
	 * @brief Gets a string value of the parameter based on idx 
	 * 
	 * @param idx The position of the parameter in the list
	 * @return std::string The string value of the parameter
	 */
	std::string  operator[](std::size_t idx) const {return _ParamValues[idx];}

	/**
	 * @brief Parses the parameter value to the given type based on T
	 * 
	 * @tparam T The type to parse the string to
	 * @param idx the position of the parameter to parse
	 * @return T The parsed value
	 */
	template<typename T> T parse(std::size_t idx) const {
		if(_ParamValues[idx].empty()){
			throw std::out_of_range("Argument " + Callees[0] + "'s parameter "  + std::to_string(idx) + " was not set!");
		}
		return ToType<T>(_ParamValues[idx]);}

	/**
	 * @brief Boolean check for if the argument was used in the function call.
	 * 
	 * @return true The argument was used
	 * @return false The argument was not used
	 */
	bool isUsed() const {
		return is_used;
	}

	/**
	 * @brief Print Argument to an ostream
	 * 
	 * @param os output stream object
	 * @param ArgBase The argument to print to the output stream object
	 * @return std::ostream& The output stream reference
	 */
	friend std::ostream& operator<<(std::ostream& os, const Argument& ArgBase){
		std::string calleeFormatted = ArgBase.GetCalleeFormatted();
		os << "\t" << std::left << std::setw(CalleeLengthBeforeDescription) << calleeFormatted;
		if(calleeFormatted.length() >= CalleeLengthBeforeDescription)
			os << std::endl << "\t" << std::string(CalleeLengthBeforeDescription, ' ');
		os << ArgBase.helpString << std::endl;	
		os << "\t" << std::right << std::setw(CalleeLengthBeforeDescription + 7) << "Usage: " << ArgBase.Callees[0];
		std::stringstream ss;
		ArgBase.FormatParameters(ss);
		os << " " << ss.str() << std::endl;
		return os;
	}
	
	/**
	 * @brief Checks if the given string callee matches with any of this argument
	 * 
	 * @param callee The callee to check
	 * @return true This argument contains this callee
	 * @return false This argument does not contain this callee
	 */
	bool operator==(const std::string& callee) const {
		return std::find(Callees.begin(), Callees.end(), callee.c_str()) != Callees.end();
	}

	/**
	 * @brief Compares Arguments based on Callees
	 * 
	 * @param A The argument to compare to
	 * @return true A is bigger than this
	 * @return false A is smaller than this
	 */
	bool operator<(const Argument& A){
		if(Callees[0].size() != A.Callees[0].size()) return Callees[0].size() < A.Callees[0].size();
		return Callees[0] < A.Callees[0];
	}

	/**
	 * @brief Compares Arguments based on Callees
	 * 
	 * @param A The argument to compare to
	 * @return true A is bigger or equal to this
	 * @return false A is smaller than this
	 */
	bool operator<=(const Argument& A){
		if(Callees[0].size() != A.Callees[0].size()) return Callees[0].size() <= A.Callees[0].size();
		return Callees[0] <= A.Callees[0];
	}

	/**
	 * @brief Compares Arguments based on Callees
	 * 
	 * @param A The argument to compare to
	 * @return true A is smaller or equal to this
	 * @return false A is bigger than this
	 */
	bool operator>=(const Argument& A){
		if(Callees[0].size() != A.Callees[0].size()) return Callees[0].size() >= A.Callees[0].size();
		return Callees[0] >= A.Callees[0];
	}

	/**
	 * @brief Compares Arguments based on Callees
	 * 
	 * @param A The argument to compare to
	 * @return true A is smaller than this
	 * @return false A is bigger than this
	 */
	bool operator>(const Argument& A){
		return !(*this < A);
	}
};

class ArgumentParser {
	std::map<std::string, Argument> Arguments;
	std::string ProgramName;
	static std::size_t Version[2];

	// Splits a string by a delimiter
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

	// generates default usage string based on required arguments and programname
	std::string defaultUsage(){
		std::stringstream ss;
		ss << "./" << ProgramName << " ";
		for(const auto& A : Arguments){
			if(A.second.required){
				ss << A.second.Callees[0];
				A.second.FormatParameters(ss);
				ss << " ";
			}
		}
		return ss.str();
	}

public:
	/**
	 * @brief Adds an argument to the list of arguments
	 * @note Callee1 and Callee2 should not be the same length! If two callees are used one should be a single character and the other a multicharacter. Order does not matter.
	 * @tparam ParamTypes The Argument data types
	 * @param Callee1 First Possible Argument Callee, Single character prefix with -, multi character prefix with --
	 * @param Callee2 Second Possible Argument Callee, Single character prefix with -, multi character prefix with --
	 * @return Argument& The argument reference
	 */
	template<typename ...ParamTypes>
	Argument& addArgument(std::string Callee1, std::string Callee2 = ""){
		auto CalleeFormatValidator = [](const std::string& Callee){
			return (Callee.size() == 0) || (Callee.size() == 2 && Callee[0] == '-' && Callee[1] != '-' && !std::isdigit(Callee[1])) || (Callee.size() > 2 && Callee[0] == '-' && Callee[1] == '-' && !isdigit(Callee[3]));
		};
		if(!CalleeFormatValidator(Callee1) || !CalleeFormatValidator(Callee2) || (Callee1.size() == Callee2.size()))
			throw std::invalid_argument("Argument callee does not follow format: Single character arguments should start with prefix -, multi character arguments should start with prefix --");		
		// If a second callee is given sort based on length
		if(!Callee2.empty())
			if(Callee2.size() < Callee1.size()) // make sure Callee1 is the shortest
				std::swap(Callee1, Callee2);

		auto [ArgumentPair_it, success] = Arguments.insert({Callee1, Argument{sizeof...(ParamTypes), Callee1, Callee2}});
		if(!success)
			throw std::runtime_error("Insertion of argument failed.");
		ArgumentPair_it->second.InitParamNamesDefault<0, ParamTypes...>();
		return ArgumentPair_it->second;
	}

	/**
	 * @brief Adds a flag to the list of arguments
	 * 
	 * @param Callee1 First Possible Argument Callee, Single character prefix with -, multi character prefix with --
	 * @param Callee2 Second Possible Argument Callee, Single character prefix with -, multi character prefix with --
	 * @return Argument& The argument reference
	 */
	Argument& addFlag(std::string Callee1, std::string Callee2 = ""){
		Argument* _flag = &addArgument<bool>(Callee1, Callee2)
			.implicitValue(true)
			.defaultValue(false);
		_flag->is_flag = true;
		return *_flag;
	}

	/**
	 * @brief Construct a new Argument Parser object
	 * 
	 * @param ProgramName The programname 
	 * @param Major Major version
	 * @param Minor Minor version
	 */
	ArgumentParser(std::string ProgramName, std::size_t Major, std::size_t Minor) : ProgramName(ProgramName){
		Version[0] = Major;
		Version[1] = Minor;

		addFlag("-V", "--Version")
			.action([&]
				(const std::vector<std::string>&){
				std::cout << "Software version: " << ArgumentParser::Version[0] << "." 
					<< ArgumentParser::Version[1] << std::endl;
				exit(1);
				})
			.Help("Displays the software version");
		addFlag("-h", "--help")
			.action([&]
				(const std::vector<std::string>&){
					std::cout << "Default Usage: " << defaultUsage() << std::endl;
					for(auto const& pair: Arguments)
						std::cout << pair.second << std::endl;
					exit(1);
				})
			.Help("Displays this message");
	}

	/**
	 * @brief 
	 * 
	 * @param argc 
	 * @param argv 
	 */
	void ParseArguments(const int argc, const char** argv){
		std::size_t ReqArgumentCount = 0;
		for(auto& pair : Arguments)
			if(pair.second.required)
				ReqArgumentCount++;
		
		std::vector<std::vector<std::string>> ArgumentData;

		// checks if a string is an argument
		auto isArgument = [](const char* string) -> bool{
			size_t len = std::strlen(string);
			return (len >= 2 && string[0] == '-' && string[1] != '-' && !std::isdigit(string[1]));
		};

		for(int i = 1; i < argc; i++){
			// check if string starts with -
			if(isArgument(argv[i])){
				// single dash with multiple arguments is a compound argument. Disect
				if(argv[i][1] != '-' && std::strlen(argv[i]) > 2){
					int k = 0; // keep track of every parameters for each compound argument;
					for(int j = 1; j < std::strlen(argv[i]); j++){ // j is argv[i] itterator start at 1 to skip -
						ArgumentData.push_back(std::vector<std::string>{"-" + std::string(1, argv[i][j])}); // add - argument for later parsing.
						auto Argpos = Arguments.find("-" + std::string(1, argv[i][j]));
						if(Argpos == Arguments.end())
							throw std::invalid_argument("Unkown console argument: -" + std::string(1, argv[i][j]) + " use -h for help");
						int l = 0;
						for(;l < Argpos->second._paramcount && i + 1 + k + l < argc; l++){
							if(isArgument(argv[i+1+k+l]))
								throw std::out_of_range("Not enough parameters for compound argument -" + std::string(1, argv[i][j]) + " use -h for help");
							ArgumentData.back().push_back(argv[i+1+k+l]);
						}
						k+=l;
						if(Argpos->second.required)
							ReqArgumentCount--;
					}
					i += k; // k is the amount of parameters parsed
				}
				else{
					ArgumentData.push_back(std::vector<std::string>{argv[i]});
					int j = 0;
					for(; i+j+1 < argc; j++){
						// check if parameter is actually an argument
						if(isArgument(argv[i+j+1]))
							break;
						ArgumentData.back().push_back(argv[i+j+1]); // add parameters
					}
					i += j;

					// remove from required Argument count
					auto Argpos = Arguments.find(argv[i]);
					if(Argpos == Arguments.end())
						throw std::invalid_argument("Unkown console argument: " + std::string(argv[i]) + " use -h for help");
					if(Argpos->second.required)
						ReqArgumentCount--;
				}
			}
		}
		// Check all required arguments were passed
		if(ReqArgumentCount != 0)
			throw std::out_of_range("Not all required arguments were passed. Default usage: " + defaultUsage());

		// Parse the arguments
		for(auto parameters : ArgumentData){
			auto Arg = Arguments.find(parameters[0]);	// no need to check as all argnames have been checked at this point.	
			parameters.erase(parameters.begin());
			Arg->second._ParseArg(parameters);
		}

	}

	Argument& operator[](std::string Arg){
		auto _Arg = Arguments.find(Arg);
		if(_Arg == Arguments.end())
			throw std::invalid_argument(Arg + " argument does not exist");
		else
			return _Arg->second;
	}
};

std::size_t ArgumentParser::Version[2];


}; // end of namespace