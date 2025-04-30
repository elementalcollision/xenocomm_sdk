# XenoComm SDK Documentation

Welcome to the XenoComm SDK documentation. This directory contains comprehensive documentation for the SDK, including API references, integration guides, and protocol specifications.

## Documentation Structure

### API Reference (`api/`)
- Complete reference documentation for all public APIs
- Generated from source code using Doxygen
- Includes class hierarchies and dependency graphs
- Examples for each major component

### Integration Guides (`guides/`)
- Step-by-step guides for common use cases
- Best practices and optimization tips
- Troubleshooting guides
- Migration guides for version updates

### Protocol Specifications (`specs/`)
- Detailed protocol specifications
- Data format descriptions
- Network protocol documentation
- Security considerations

## Building the Documentation

The documentation is built using Doxygen for API reference and Markdown for guides and specifications.

### Prerequisites
- Doxygen 1.9.0 or newer
- Graphviz (for generating diagrams)
- Python 3.8+ (for documentation tooling)

### Building

```bash
# From the project root
cd docs
doxygen Doxyfile

# Documentation will be generated in api/html/
```

## Contributing to Documentation

We welcome improvements to the documentation! Please see the [Contributing Guidelines](../CONTRIBUTING.md) for details on our documentation standards and process.

When contributing to documentation:
1. Ensure accuracy and clarity
2. Include practical examples
3. Keep the style consistent
4. Test any code examples
5. Update the table of contents if needed

## Documentation TODOs

- [ ] Complete API reference documentation
- [ ] Add more code examples
- [ ] Create troubleshooting guide
- [ ] Add performance optimization guide
- [ ] Create security best practices guide 