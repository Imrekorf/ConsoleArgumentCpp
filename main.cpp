#include "ArgumentParser.hpp"

using namespace ArgPar;

// ProgramName --arg param1 ... paramX

int main(int argc, const char* argv[]){

	ArgumentParser AP("ArgumentParser", 1, 0);

	AP.addArgument<int, float, char, unsigned int>("-I")
		.Help("This is a help message");
	AP.addArgument<int, float>("-J")
		.Help("This is a help message2");

	Argument Flag = AP.addFlag("--Flag", "-F");

	AP.ParseArguments(argc, argv);

	std::cout << AP["-I"].Parse<int>(0) << std::endl;
	std::cout << AP["-I"].Parse<float>(1) << std::endl;
	std::cout << AP["-I"].Parse<char>(2) << std::endl;
	std::cout << AP["-I"].Parse<unsigned int>(3) << std::endl;
	std::cout << AP["-J"].Parse<int>(0) << std::endl;
	std::cout << AP["-J"].Parse<float>(1) << std::endl;
	std::cout << AP["-F"].IsUsed() << std::endl;

	return 0;
}
