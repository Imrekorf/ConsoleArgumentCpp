# ConsoleArgumentCpp
A C++ Console argument parser a more streamlined console argument parser than my [original parser](https://github.com/Imrekorf/ConsoleArgumentParser). 
Based on [ArgParse](https://github.com/p-ranav/argparse)

Instantiation of the argument parser is done by creating an ArgumentParser object and passing a programname, major and minor version.<br>
```c++
ArgumentParser AP("ArgumentParser", 1, 0);
```
Afterwards arguments can be added by using the addArgument function. Here 1 or 2 callees can be passed, these are the names of the argument which are used in the CLI.
The callees use the single dash for one character and two dash for multi character argument names. Which callee comes first does not matter, as long as 1 callee is specified, multi or single character.
If the passed callee strings do not conform to this format an invalid_argument exception is thrown.
An argument alway has parameters passed along with the argument. The types of these parameters should be specified with the argument.
```c++
AP.addArgument<int, float>("-E", "--Example);
AP.addArgument<std::string, std::string>("-U");
```
After creating the argument details can be appended to the addArgument call:
```c++
AP.addArgument<int>("-I").Help("this is a help message");
```
Any detail function will return a reference to the argument so constructions like below are possible:
```
AP.addArgument<int>("-I")
  .Help("this is a help message")
  .Required()
  .ParameterName("number")
  .defaultValue(25);
```
Available detail functions and their function:
| Name | Description | Usage |
| ---- | ----------- | ----- |
| ParameterName | Sets the names of the parameters for the usage message displayed in help | ``` .ParameterName("Param1", "Param2", ... "ParamN");``` |
| DefaultValue | Sets the default value for the parameter. If the parameter is not or partially passed these values are used when accessing the parameter values | ```.defaultValue(25, 35.5, "string");``` |
| ImplicitValue | Sets the implicit value for the parameter. If the parameter argument callee is passed with 0 arguments and there are implicit values these are used instead of default values | ```.implicitValue(15, 68.5, "nostring");``` |
| Help | Sets the help string for the argument | ```.Help("This is a description about the argument");``` |
| Required | Marks the argument as required such that an error is generated during CLI parsing if it was not passed | ```.Required()``` |
| Action | Sets a custom parser for if the argument is called, the custom parser gets send the list of parameters passed through the CLI. Function should return void and accept the parameters as a vector of strings.  | ```.Action(function)``` | 
| Validator | Sets a custom validator function that is called at the start of the of the arguments parse function, Function should return 0 if all parameters are valid or the position of the 1st parameter that failed the validator. Function gets passed the parameters as a vector of strings | ```.Validator(function)``` |

To parse incoming arguments use:
```C++
AP.ParseArguments(argc, argv);
```
Argument parsing first checks for a validator, afterwards a 

Afterwards the arguments parameters can be accessed by:
```C++ 
AP["-I"].Parse<int>(0); // get int value of 1st parameter of argument -I
AP["-I"].isUsed(); // returns true if -I was passed through the CLI
```
Trying to access a parameter which was not passed and does not have a default value will result in an out_of_range exception.

Flags are also supported and support the same detail functions as arguments.
Flags by default have a default value of false and an implicit value of true. 
Flags can be added by using:
```
AP.addFlag("-F"); // or pass two callees like normal arguments
```

