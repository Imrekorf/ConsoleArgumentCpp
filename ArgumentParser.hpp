#include <functional>
#include <algorithm>
#include <exception>
#include <iostream>
#include <utility>
#include <iomanip>
#include <sstream>
#include <cstring> // for strlen
#include <string>
#include <vector>
#include <limits>
#include <tuple>
#include <map>

#define CalleeLengthBeforeDescription 22

namespace ArgPar {

class ValidatorException : public std::exception {
	const std::string _message;
	const std::string _ArgumentName;
	const std::size_t _ArgumentPosition;

public:
	ValidatorException(std::string msg, const std::string ArgumentName, const std::size_t ArgumentPosition) 
		: _message(msg), _ArgumentName(ArgumentName), _ArgumentPosition(ArgumentPosition) {}
	
	const char* what() const noexcept override { return _message.c_str(); }
	const std::string ArgumentName() const {return _ArgumentName;}
	std::size_t ArgumentPosition() const {return _ArgumentPosition;}
};

class MissingRequiredParameter : public std::exception {
	const std::string _message;
	const std::vector<std::string> _missingArguments;

public:
	MissingRequiredParameter(std::string msg, std::vector<std::string> missingArguments) 
		: _message(msg), _missingArguments(missingArguments) {}
	
	const char* what() const noexcept override { return _message.c_str(); }
	const std::vector<std::string> missingArguments() const {return _missingArguments;}
};


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

/**
 * @brief Converts a string to a type T
 * Only converts if the class has a stream >> operator implementation. Otherwise throws an invalid argument exception
 * @tparam T type to conver to
 * @param s the string to convert to T
 * @return const T the converted value
 * @throws invalid_argument exception if the conversion fails. This can occur if the string is empty
 */
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

	
	bool required = false;
	bool has_implicitValues = false;
	bool has_defaultValues = false;
	bool is_flag = false;
	bool is_used = false;
	bool parseAlways = false;
	bool needs_parameters = true;

	std::size_t _priority = 0;

	std::size_t _paramcount = 0;

	std::vector<std::string> Callees;
	std::string helpString = "Look at me, I forgot to add a help string!";

	std::vector<std::string> _ParamValues;
	std::vector<std::string> _ParamImplicitValues;
	std::vector<std::string> _ParamNames;

	std::function<void(const std::vector<std::string>&)> _f_ArgumentAction = nullptr;
	std::function<std::size_t(const std::vector<std::string>&)> _f_ParameterParserValidator = nullptr;
	// Gets the type of the ParamTypes parameter pack at index I
	template<std::size_t I, typename ...ParamTypes>
	using TupleTypeAt = typename std::tuple_element<I, std::tuple<ParamTypes...>>::type;
	
	// Formats parameter names and default values if present
	void FormatParameters(std::stringstream& ss) const{
		if(is_flag)
			return;
		for(std::size_t i = 0; i < _paramcount; i++){
			ss << "[" << _ParamNames[i];
			if(has_implicitValues){
				std::string implicitStringValue = _ParamImplicitValues[i];
				// remove trailing 0s
				if(implicitStringValue.find('.') != std::string::npos){
					implicitStringValue = implicitStringValue.substr(0, implicitStringValue.find_last_not_of('0')+1);
					if(implicitStringValue.find('.') == implicitStringValue.size()-1)
						implicitStringValue = implicitStringValue.substr(0, implicitStringValue.size()-1);
				}
				ss << ": " << implicitStringValue;
			}
			ss << "] ";
		}
		if(has_defaultValues){
			ss << " default: ";
			for(std::size_t i = 0; i < _paramcount; i++)
				ss << _ParamNames[i] << "(" << _ParamValues[i] << ") ";
		}
	}

	// Formats the Callee's
	std::string GetCalleeFormatted() const {
		std::string calleeFormatted;
		for(std::size_t i = 0; i < Callees.size() - 1; i++){
			calleeFormatted += Callees[i] + ", ";
		}
		calleeFormatted += Callees.back();
		return calleeFormatted;
	}

	//?==== Argument parser logic ====?//
	/**
	 * @brief Parses parameters given to the argument
	 * First sets up a buffer containing the values based on implicit parameter values or default values, then performs a validator if set and then calls the custom function set by .Action() if set.
	 * Implicit values are used over default values if not all parameter values are specified.
	 * @param Parameters The list of parameters passed through CLI
	 * @throws out_of_range exception if not enough parameters are passed and no implicit or default values are specified.
	 * @throws ValidatorException exception if the passed parameter values do not pass the custom validator function. Only applies if validator function is specified.
	 */
	void _ParseArg(std::vector<std::string>& Parameters){
		if(!needs_parameters)
			_f_ArgumentAction({}); // optimatisation for information arguments
		// Set up vector containing the correct parameter values
		std::vector<std::string> tempParamValues(_ParamValues.size());
		std::copy(_ParamValues.begin(), _ParamValues.end(), tempParamValues.begin());
		is_used = true;
		// If there are implicit values and no parameters given, use implicit values
		if(has_implicitValues && Parameters.size() == 0)
			std::copy(_ParamImplicitValues.begin(), _ParamImplicitValues.end(), tempParamValues.begin());
		else{
			auto ParamCopyIt = std::copy(Parameters.begin(), Parameters.end(), tempParamValues.begin());
			if(ParamCopyIt != tempParamValues.end()){ // Not all parameter values were passed
				std::size_t CopiedCount = ParamCopyIt - tempParamValues.begin();
				std::cout << CopiedCount << std::endl;
				if(has_implicitValues) // we have implicit values through!
					std::copy(_ParamImplicitValues.begin() + CopiedCount, _ParamImplicitValues.end(), ParamCopyIt);
				else if(has_defaultValues) // or we have default values though!
					std::copy(_ParamValues.begin() + CopiedCount, _ParamValues.end(), ParamCopyIt);
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
		// If there is a validator, execute validator
		if(_f_ParameterParserValidator){
			std::size_t pos = _f_ParameterParserValidator(tempParamValues);
			if(pos)
				throw ValidatorException(("Validator for argument: " + Callees[0] + " failed at position " + std::to_string(pos)), Callees[0], pos);
		}

		// If there is a custom parser, execute that instead
		if(needs_parameters && _f_ArgumentAction)
			_f_ArgumentAction(tempParamValues);
		// Copy the values if everything went well
		std::copy(tempParamValues.begin(), tempParamValues.end(), _ParamValues.begin());
		
	}

	// Init parameter names based on variadic list, this creates default param names
	template<std::size_t I = 0, typename ...ParamTypes>
	inline typename std::enable_if<I  < sizeof...(ParamTypes), void>::type InitParamNamesDefault(){
		_ParamNames[I] = get_type_name<TupleTypeAt<I, ParamTypes...>>();
		InitParamNamesDefault<I+1, ParamTypes...>();
	}
	// SFINAE
	template<std::size_t I = 0, typename ...ParamTypes>
	inline typename std::enable_if<I == sizeof...(ParamTypes), void>::type InitParamNamesDefault(){}

	// Init implicit values based on variadic list
	template<std::size_t I = 0, typename ...ParamTypes>
	typename std::enable_if<I  < sizeof...(ParamTypes), void>::type implicit_value(std::tuple<ParamTypes...> t){
		std::stringstream ss;
		ss << std::get<I>(t);
		if(ss.fail() | ss.bad())
			throw std::invalid_argument("Implicit value for argument " + Callees[0] + " at position " + std::to_string(I) + " is invalid, "\
										"Conversion from " + get_type_name<TupleTypeAt<I, ParamTypes...>>() + " to string failed.");
		_ParamImplicitValues[I] = ss.str();
		implicit_value<I+1, ParamTypes...>(t);
	}
	//SFINAE
	template<std::size_t I = 0, typename ...ParamTypes>
	typename std::enable_if<I == sizeof...(ParamTypes), void>::type implicit_value(std::tuple<ParamTypes...> t){(void)(t);}

	// Init default values based on variadic list
	template<std::size_t I = 0, typename ...ParamTypes>
	typename std::enable_if<I  < sizeof...(ParamTypes), void>::type default_value(std::tuple<ParamTypes...> t){
		std::stringstream ss;
		ss << std::get<I>(t);
		if(ss.fail() | ss.bad())
			throw std::invalid_argument("Default value for argument " + Callees[0] + " at position " + std::to_string(I) + " is invalid, "\
										"Conversion from " + get_type_name<TupleTypeAt<I, ParamTypes...>>() + " to string failed.");
		_ParamValues[I] = ss.str();
		default_value<I+1, ParamTypes...>(t);
	}
	//SFINAE end condition
	template<std::size_t I = 0, typename ...ParamTypes>
	typename std::enable_if<I == sizeof...(ParamTypes), void>::type default_value(std::tuple<ParamTypes...> t){(void)(t);}

	// Init parameter names based on variadic list
	template<std::size_t I = 0, typename ...ParamTypes>
	typename std::enable_if<I  < sizeof...(ParamTypes), void>::type parameter_name(std::tuple<ParamTypes...> t){
		_ParamNames[I] = std::get<I>(t);
		parameter_name<I+1, ParamTypes...>(t);
	}
	//SFINAE
	template<std::size_t I = 0, typename ...ParamTypes>
	typename std::enable_if<I == sizeof...(ParamTypes), void>::type parameter_name(std::tuple<ParamTypes...> t){(void)(t);}

public:
	
	/**
	 * @brief Construct a new Argument object
	 * 
	 * @param paramcount The amount of parameters this argument will use
	 * @param ArgName First Possible Argument Callee, Single character prefix with -, multi character prefix with --
	 * @param ArgName2 Second Possible Argument Callee, Single character prefix with -, multi character prefix with --
	 */
	Argument(std::size_t paramcount, std::string ArgName, std::string ArgName2 = "") 
		: _paramcount(paramcount), Callees(1), _ParamValues(paramcount), 
		  _ParamImplicitValues(paramcount), _ParamNames(paramcount){
		Callees[0] = ArgName;
		if(!ArgName2.empty())
			Callees.push_back(ArgName2);
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
	Argument& DefaultValue(ParamTypes... defaultValues){
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
	Argument& ImplicitValue(ParamTypes... implicitValues){
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
	 * @brief Sets the parse always property
	 * If an argument is set to be always parsed then even if not all required arguments are passed this argument will still be parsed.
	 * @return Argument& The argument reference
	 */
	Argument& ParseAlways(){parseAlways = true; return *this;}

	/**
	 * @brief Sets a custom argument parser function. 
	 * Gets called when the argument is being parsed instead of the default parser. Has access to the parameter vector that would normally be parsed for the argument.
	 * @param Action The function of the custom argument parser
	 * @param needs_parameters if the action does not need any parameters, for example information flags, then this can be set to false to execute the action function before parsing, giving slight performance improvements
	 * @return Argument& The argument reference
	 */
	Argument& Action(std::function<void(const std::vector<std::string>&)> Action, bool needs_parameters = true){
		this->needs_parameters = needs_parameters;
		_f_ArgumentAction = Action;
		return *this;
	}
	
	/**
	 * @brief Sets a custom validator function. 
	 * Gets called at the beginning of an arguments parsing function. Should return 0 if validation is passed otherwise the position of the argument that did not pass the validator check
	 * @param Validator The function of the custom validator
	 * @return Argument& The argument reference
	 */
	Argument& Validator(std::function<std::size_t(const std::vector<std::string>&)> Validator){
		_f_ParameterParserValidator = Validator;
		return *this;
	}

	/**
	 * @brief Sets the priority of the argument
	 * Arguments can be sorted by priority, A higher priority means it will be handled before lower priority arguments.
	 * Same level priority arguments are sorted based on input
	 * @param priority The priority of the argument
	 * @return Argument& The argument reference
	 */
	Argument& priority(std::size_t priority){
		_priority = priority;
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
	 * @throws out_of_range exception if idx is bigger or equal to the size of the parameter list
	 * @throws out_of_range exception if stored parameter value is an empty string.
	 */
	template<typename T> T Parse(std::size_t idx) const {
		if(idx >= _ParamValues.size())
			throw std::out_of_range("Argument " + Callees[0] + "'s parameter "  + std::to_string(idx) + " is out of range!");
		if(_ParamValues[idx].empty())
			throw std::out_of_range("Argument " + Callees[0] + "'s parameter "  + std::to_string(idx) + " was not set!");
		return ToType<T>(_ParamValues[idx]);}

	/**
	 * @brief Boolean check for if the argument was used in the function call.
	 * 
	 * @return true The argument was used
	 * @return false The argument was not used
	 */
	bool IsUsed() const {
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
		std::string tempHelpString = ArgBase.helpString;
		for(std::size_t pos = 0; (pos = tempHelpString.find("\n", pos)) != std::string::npos; pos+=CalleeLengthBeforeDescription+1)
			tempHelpString.replace(pos, 1, "\n\t" + std::string(CalleeLengthBeforeDescription, ' '));
		os << tempHelpString << std::endl;	
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
		return std::find(Callees.begin(), Callees.end(), callee) != Callees.end();
	}
};

class ArgumentParser {
	std::map<std::string, Argument> Arguments;
	std::string ProgramName;
	std::size_t Version[2];

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
				ss << A.second.Callees[0] << " ";
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
	 * @throws runtime_error exception if std::map<std::string, Argument>.insert() failed
	 */
	template<typename ...ParamTypes>
	Argument& addArgument(std::string Callee1, std::string Callee2 = ""){
		static_assert(sizeof...(ParamTypes) > 0, "addArgument Requires atleast 1 template parameter");
		auto CalleeFormatValidator = [](const std::string& Callee){
			return (Callee.size() == 0) || (Callee.size() == 2 && Callee[0] == '-' && Callee[1] != '-' && !std::isdigit(Callee[1])) || (Callee.size() > 2 && Callee[0] == '-' && Callee[1] == '-' && !isdigit(Callee[3]));
		};
		if(!CalleeFormatValidator(Callee1) || !CalleeFormatValidator(Callee2) || (Callee1.size() == Callee2.size()))
			throw std::invalid_argument("Argument callee does not follow format: Single character arguments should start with prefix -, multi character arguments should start with prefix --");		
		// If a second callee is given sort based on length
		if(!Callee2.empty())
			if(Callee2.size() < Callee1.size()) // make sure Callee1 is the shortest
				std::swap(Callee1, Callee2);

		auto insert_pair_ret = Arguments.insert({Callee1, Argument{sizeof...(ParamTypes), Callee1, Callee2}});
		if(!insert_pair_ret.second)
			throw std::runtime_error("Insertion of argument failed, maybe the Callee is already used.");
		insert_pair_ret.first->second.InitParamNamesDefault<0, ParamTypes...>();
		return insert_pair_ret.first->second;
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
			.ImplicitValue(true)
			.DefaultValue(false);
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
			.Action([&]
				(const std::vector<std::string>&){
				std::cout << "Software version: " << ArgumentParser::Version[0] << "." 
					<< ArgumentParser::Version[1] << std::endl;
				exit(0);
				}, false)
			.Help("Displays the software version")
			.ParseAlways()
			.priority(std::numeric_limits<std::size_t>::max());
		addFlag("-h", "--help")
			.Action([&]
				(const std::vector<std::string>&){
					std::cout << "Default Usage: " << defaultUsage() << std::endl;
					for(auto const& pair: Arguments)
						std::cout << pair.second << std::endl;
					exit(0);
				}, false)
			.Help("Displays this message")
			.ParseAlways()
			.priority(std::numeric_limits<std::size_t>::max());
	}

	/**
	 * @brief Parses the command line arguments
	 * 
	 * @param argc The given argument count
	 * @param argv The list of argument values
	 * 
	 * @throws invalid_argument exception if a passed argument is unknown
	 * @throws out_of_range exception if a compound argument list does not contain enough parameters
	 * @throws MissingRequiredParameter if any required parameters are missing
	 */
	void ParseArguments(const int argc, const char** argv){
		std::size_t ReqArgumentCount = 0;
		for(auto& pair : Arguments)
			if(pair.second.required)
				ReqArgumentCount++;
		
		std::map<
			std::pair<std::size_t, std::size_t>, 
			std::pair<Argument*, std::vector<std::string>>, 
			std::greater<std::pair<std::size_t, std::size_t>>> ArgumentData;
		std::map<
			std::pair<std::size_t, std::size_t>, 
			std::pair<Argument*, std::vector<std::string>>, 
			std::greater<std::pair<std::size_t, std::size_t>>> ParseAlwaysArguments;

		// checks if a string is an argument
		auto isArgument = [](std::string Callee) -> bool{
			return (Callee.size() == 2 && Callee[0] == '-' && Callee[1] != '-' && !std::isdigit(Callee[1])) 
				|| (Callee.size() > 2 && Callee[0] == '-' && !isdigit(Callee[1]));
		};

		std::size_t w = 0;
		for(std::size_t i = 1; i < (std::size_t)argc; i++){
			// check if string starts with -
			if(isArgument(argv[i])){
				// single dash with multiple arguments is a compound argument. Disect
				if(argv[i][1] != '-' && std::strlen(argv[i]) > 2){
					std::size_t k = 0; // keep track of every parameters for each compound argument;
					for(std::size_t j = 1; j < std::strlen(argv[i]); j++){ // j is argv[i] itterator start at 1 to skip -
						auto Argpos = Arguments.find("-" + std::string(1, argv[i][j]));
						if(Argpos == Arguments.end())
							throw std::invalid_argument("Unkown console argument: -" + std::string(1, argv[i][j]) + " use -h for help");
						auto insertRef = ArgumentData.insert({{Argpos->second._priority, w++}, std::make_pair(&(Argpos->second), std::vector<std::string>(0))}); // add - argument for later parsing.
						if(!insertRef.second)
							throw std::runtime_error("Insertion of argument failed, maybe the key is already used.");
						std::size_t l = 0;
						for(;l < Argpos->second._paramcount && i + 1 + k + l < (std::size_t)argc; l++){
							if(isArgument(argv[i+1+k+l]))
								throw std::out_of_range("Not enough parameters for compound argument " + std::string(argv[i]) + " use -h for help");
							insertRef.first->second.second.push_back(argv[i+1+k+l]);
						}
						// add to parseAlways if needed
						if(Argpos->second.parseAlways)
							ParseAlwaysArguments.insert(*insertRef.first);
						k+=l;
						if(Argpos->second.required)
							ReqArgumentCount--;
					}
					i += k; // k is the amount of parameters parsed
				}
				else{
					// Find argument
					auto Argpos = std::find_if(Arguments.begin(), Arguments.end(), [&](std::pair<std::string, ArgPar::Argument> A){
						return A.second == argv[i];
					});
					if(Argpos == Arguments.end())
						throw std::invalid_argument("Unkown console argument: " + std::string(argv[i]) + " use -h for help");
					// Remove from required Argument count
					if(Argpos->second.required)
						ReqArgumentCount--;
					auto insertRef = ArgumentData.insert({{Argpos->second._priority, w++}, std::make_pair(&(Argpos->second), std::vector<std::string>(0))});
					if(!insertRef.second)
						throw std::runtime_error("Insertion of argument failed, maybe the key is already used.");
					std::size_t j = 0;
					for(; i+j+1 < (std::size_t)argc; j++){
						// check if parameter is actually an argument
						if(isArgument(argv[i+j+1]))
							break;
						insertRef.first->second.second.push_back(argv[i+j+1]); // add parameters
					}
					// add to parseAlways if needed
					if(Argpos->second.parseAlways)
						ParseAlwaysArguments.insert(*insertRef.first);

					i += j;
				}
			}
		}
		// Check all required arguments were passed
		if(ReqArgumentCount != 0){
			for(auto p : ParseAlwaysArguments)
				p.second.first->_ParseArg(p.second.second); // Parse the "parse always" argument regardless of required arguments.
			std::vector<std::string> missingArguments;
			for(auto p : Arguments){
				if(p.second.required){
					// find the required argument in the passed arguments
					auto it = std::find_if(ArgumentData.begin(), ArgumentData.end(), [&](auto A){
						return &p.second == A.second.first; // We can use pointers as both arguments are from the same list
					});
					// if it was not found add it
					if(it == ArgumentData.end())
						missingArguments.push_back(p.second.Callees[0]);
				}
			}
			throw MissingRequiredParameter("Not all required arguments were passed. Default usage: " + defaultUsage(), missingArguments);		
		}

		// Parse the arguments
		for(auto _Argument : ArgumentData){
			_Argument.second.first->_ParseArg(_Argument.second.second);
		}

	}

	/**
	 * @brief Returns a reference to an argument specified by the key
	 * 
	 * @param ArgKey The key of the argument
	 * @return Argument& A reference to the argument
	 * @throws invalid_argument exception if the argument key does not exist
	 */
	Argument& operator[](std::string ArgKey){
		auto _Arg = std::find_if(Arguments.begin(), Arguments.end(), [ArgKey](const auto& pair){
			return pair.second == ArgKey;
		});
		if(_Arg == Arguments.end())
			throw std::invalid_argument(ArgKey + " argument does not exist");
		else
			return _Arg->second;
	}
};


} // end of namespace

#undef CalleeLengthBeforeDescription