#include <catch2/catch_session.hpp>

#include <iostream>
#include <string>
#include <vector>
#include <cctype> // for std::tolower

// Main function to handle --yes/-y flag and prompt user confirmation before running tests
int main(int argc, char* argv[])
{
    std::vector<char*> new_argv;
    bool force_yes = false;

    for (int i = 0; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--yes" || arg == "-y")
        {
            force_yes = true;
            continue;
        }
        new_argv.push_back(argv[i]);
    }

    if (!force_yes)
    {
        std::cout << "Caution: This will modify the connected RealSenseID device configuration and database. Continue? (y/n): ";
        std::string input;
        std::getline(std::cin, input);

        if (input.empty() || (std::tolower(static_cast<unsigned char>(input[0])) != 'y'))
        {
            std::cerr << "Test execution aborted by user." << std::endl;
            return 1;
        }
    }

    int new_argc = static_cast<int>(new_argv.size());
    return Catch::Session().run(new_argc, new_argv.data());
}