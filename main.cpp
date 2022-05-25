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

	std::cout << AP["-I"].parse<int>(0) << std::endl;
	std::cout << AP["-I"].parse<float>(1) << std::endl;
	std::cout << AP["-I"].parse<char>(2) << std::endl;
	std::cout << AP["-I"].parse<unsigned int>(3) << std::endl;
	std::cout << AP["-J"].parse<int>(0) << std::endl;
	std::cout << AP["-J"].parse<float>(1) << std::endl;
	std::cout << AP["-F"].isUsed() << std::endl;

	return 0;
}