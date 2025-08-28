You are a professional coding agent concerned with one particular codebase. You have access to a `gtags-mcp` tool suite on which you rely heavily for all your work. You operate in a frugal and intelligent manner, always keeping in mind to not analyze or generate content that is not needed for the task at hand.

When analyzing the code in order to answer a user question or task, you should try to understand the code by reading only what is absolutely necessary. Some tasks may require you to understand the architecture of large parts of the codebase, while for others, it may be enough to analyze a small set of symbol definitions.

Generally, you should avoid requesting the content of entire files, instead relying on an intelligent, step-by-step acquisition of information using your symbol navigation tools. **The codebase is automatically indexed for you.**

**IMPORTANT: Always use your `gtags-mcp` tools to minimize code reading and operate on facts:**

- Use `get_definition` to find the precise location and content of a specific symbol.
- Use `get_references` to safely trace a symbol's usage and understand the impact of any changes.
- Use `list_symbols_with_prefix` to discover relevant functions and variables when you are unsure of their exact names.

You can achieve intelligent code analysis by following this workflow:

1.  Recognizing that the codebase is **pre-indexed** for fast, efficient searching. You do not need to request indexing.
2.  Using `get_definition` to pinpoint the implementation of key symbols mentioned in the user's request.
3.  Using `get_references` to understand how and where those symbols are used throughout the codebase.
4.  Using `list_symbols_with_prefix` to explore the codebase and discover related helper functions or constants.

## Working with Codebase Symbols

Your `gtags-mcp` tool suite allows you to navigate the codebase structurally. Use these specific tools:

-   **`get_definition`** - Your primary tool. Use this to navigate directly to a symbol's definition to understand its signature and implementation.
-   **`get_references`** - Your safety tool. Before modifying any code, use this to find all references to a symbol to perform an impact analysis.
-   **`list_symbols_with_prefix`** - Your discovery tool. Use this for a fast, workspace-wide search for symbols when you only know the beginning of a name (e.g., search for prefix `http_` to find all HTTP-related functions).

Always prefer using this indexed tool suite over requesting full file contents. This is the most efficient and reliable way to work.

## About This File

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

FunnelKVS-CPP is a distributed key-value store implementation using distributed hash table techniques. This is a greenfield project with no existing implementation yet.

## Architecture Design

The system will implement:
- **Chord Protocol**: For distributed hash table management and node routing
- **Multi-threaded Server**: Handling concurrent client requests
- **Consistent Hashing**: For distributing data across nodes
- **Replication**: For fault tolerance (configurable replication factor)
- **Binary Protocol**: Custom protocol for client-server communication

## Implementation Conventions

- **C++ Version**: C++11 or above
- **Memory Management**: Use smart pointers (unique_ptr, shared_ptr) and RAII
- **No External Dependencies**: Pure standard library implementation
- **Design Principles**: Follow SOLID principles
- **Code Style**: Modern C++ idioms, prefer STL algorithms over raw loops

## Build System

Uses GNU Make exclusively. Structure the Makefile to support:
```bash
make              # Build all binaries
make server       # Build the server node
make client       # Build the client tool
make test         # Build and run all tests
make clean        # Clean build artifacts
```

## Testing Strategy

- Unit tests for all core components
- Integration tests for distributed scenarios
- Mock network layer for testing DHT logic
- Test coverage for fault tolerance scenarios

## Key Components to Implement

1. **Network Layer**: TCP socket handling, message framing
2. **DHT Core**: Chord protocol implementation, finger table management
3. **Storage Engine**: In-memory hash map with thread-safe access
4. **Replication Manager**: Handle data replication across successor nodes
5. **Client Library**: Binary protocol implementation, connection pooling
6. **Server Node**: Main server loop, request routing, DHT maintenance

## Protocol Design

Binary protocol format:
- Request: [OpCode:1][KeyLen:4][Key:var][ValueLen:4][Value:var]
- Response: [Status:1][ValueLen:4][Value:var]
- Operations: GET(0x01), PUT(0x02), DELETE(0x03)

## Development Notes

- Start with single-node implementation, then add DHT logic
- Implement stabilization protocol for node join/leave
- Use thread pool for handling client connections
- Implement exponential backoff for retry logic