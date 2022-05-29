#include "ArgumentParser.hpp"

// compile with g++ main.cpp
// See usage with -h

using namespace ArgPar;

// ProgramName --arg param1 ... paramX

#define GIT_COMMIT "00000"

int main(int argc, const char* argv[]){

	ArgumentParser AP("ArgumentParser", 1, 0);

	// Example of an argument with multiple parameters with implicit and default values.
	AP.addArgument<int, float, char, unsigned int>("-I")
		.Help("I can handle up to 4 parameters.¨"\
			  "I don't need to be passed to have values because of my default values, "\
			  "you can also pass between 0 and 4 parameters after which my other values are set by implicit values!")
		.ImplicitValue(10, 0.5, 'h', -404)				
		.DefaultValue(0, 0, 0, 0);

	// example of an argument with validator to check if value is between 0 and 10
	AP.addArgument<int>("-J")
		.Help("I need to be passed because I am required. If Im not passed a MissingRequiredParameter exception will be thrown. Also because Im required I dont need default values!")
		.Validator([](const std::vector<std::string>& parameters){
			return !(ToType<int>(parameters[0]) > 0 && ToType<int>(parameters[0]) < 10);
		})
		.Required();

	// Example of simple flag
	AP.addFlag("--Flag", "-F").Help("Im a flag on which you can later check if Im passed");

	// Example for a flag with an action after parsing. Needs_Parameters is set to false to optimise runtime
	AP.addFlag("-G", "--GitCommit")
		.Help("Displays the git commit hash this software was build with")
		.Action([](const std::vector<std::string>&){
				std::cout << "Software build with git commit: " << std::hex << GIT_COMMIT << std::endl;
				exit(0);
		}, false);
	
	// example of keeping a reference to the created argument.
	const Argument& Flag = AP.addFlag("-f", "--flag");

	try{
		AP.ParseArguments(argc, argv);
	}
	catch(const ValidatorException& VE){
		std::cout << "Validation for " << VE.ArgumentName() << " failed at position: " << VE.ArgumentPosition() << std::endl;
		return -1;
	}

	std::cout << "-I: ";
	std::cout << AP["-I"].Parse<int>(0) << " ";
	std::cout << AP["-I"].Parse<float>(1) << " ";
	std::cout << AP["-I"].Parse<char>(2) << " ";
	std::cout << AP["-I"].Parse<unsigned int>(3) << std::endl;
	
	std::cout << "-J: " << AP["-J"].Parse<int>(0) << std::endl;
	
	std::cout << "-F: "<< AP["-F"].IsUsed() << std::endl;

	std::cout << "-f: " << Flag.IsUsed() << std::endl;

	return 0;
}
