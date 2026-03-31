---
name: Always write tests for new code
description: User expects new tests to be written alongside any new functionality
type: feedback
---

Always write tests for new functionality. When adding or changing behaviour, add corresponding test cases in the relevant `tests/test_*.cpp` file and register them in `main()` in the same commit.

**Why:** User explicitly requested this as a standing rule.

**How to apply:** For every new function, feature, or behaviour change, add tests covering it in the appropriate `tests/test_*.cpp` file and register them in `main()`.
