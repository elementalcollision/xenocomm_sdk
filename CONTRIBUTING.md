# Contributing to XenoComm SDK

We love your input! We want to make contributing to XenoComm SDK as easy and transparent as possible, whether it's:

- Reporting a bug
- Discussing the current state of the code
- Submitting a fix
- Proposing new features
- Becoming a maintainer

## Development Process

We use GitHub to host code, to track issues and feature requests, as well as accept pull requests.

1. Fork the repo and create your branch from `main`.
2. If you've added code that should be tested, add tests.
3. If you've changed APIs, update the documentation.
4. Ensure the test suite passes.
5. Make sure your code follows our coding standards.
6. Issue that pull request!

## Code Style

- Use C++17 features where appropriate
- Follow the existing code style
- Use meaningful variable and function names
- Write clear comments for complex logic
- Document public APIs using Doxygen-style comments

### Formatting

We use clang-format for code formatting. The configuration is in the `.clang-format` file.

```bash
# Format a file
clang-format -i path/to/file.cpp

# Format all files
find src include tests -iname '*.hpp' -o -iname '*.cpp' | xargs clang-format -i
```

### Static Analysis

We use clang-tidy for static analysis. The configuration is in the `.clang-tidy` file.

```bash
# Run clang-tidy on a file
clang-tidy path/to/file.cpp

# Run on all files (from build directory)
run-clang-tidy
```

## Testing

- Write unit tests for new code using Google Test
- Include both positive and negative test cases
- Test edge cases and error conditions
- Run the full test suite before submitting a PR:

```bash
# From build directory
ctest --output-on-failure
```

## Documentation

- Document all public APIs using Doxygen-style comments
- Update relevant documentation in the `docs/` directory
- Include examples for new features
- Keep the README.md up to date

## Pull Request Process

1. Update the README.md with details of changes to the interface
2. Update the documentation with details of any new functionality
3. The PR must pass all CI checks
4. You may merge the PR once you have the sign-off of at least one other developer

## Any contributions you make will be under the MIT Software License

In short, when you submit code changes, your submissions are understood to be under the same [MIT License](LICENSE) that covers the project. Feel free to contact the maintainers if that's a concern.

## Report bugs using GitHub's [issue tracker](https://github.com/yourusername/xenocomm_sdk/issues)

We use GitHub issues to track public bugs. Report a bug by [opening a new issue](https://github.com/yourusername/xenocomm_sdk/issues/new).

### Write bug reports with detail, background, and sample code

**Great Bug Reports** tend to have:

- A quick summary and/or background
- Steps to reproduce
  - Be specific!
  - Give sample code if you can
- What you expected would happen
- What actually happens
- Notes (possibly including why you think this might be happening, or stuff you tried that didn't work)

## License

By contributing, you agree that your contributions will be licensed under its MIT License. 