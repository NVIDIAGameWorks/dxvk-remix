# RTX Remix Unit Test

Remix Unit Tests serve the purpose of evaluating the smallest functional components of operators or algorithms within Remix.
These tests currently focus on the important SSE operations and math algorithms used in Remix.

## Guidelines for Introducing Unit Tests

1. Add your test code to the folder (tests/rtx/unit). Don't forget to also include the new file in the project's build script, which is [tests/rtx/unit/meson.build].
2. Write straightforward code that directly invokes the functions being tested. Check result for each test, and if all tests yield the intended outcomes, the main function should return 0. In cases where any test fails, return -1.

## Requirements

- The new unit tests should have minimal number of dependencies. If there are too many dependencies, do refactoring on the code that is tested, and make sure it's compatible to the unit test.
- It is essential that the unit tests offer complete coverage. This requires a carefully design of the test data to cover every possible branch and scenario. Make sure the expected result are correct. Moreover, for ease of debugging, put comments with details about each test.
- In cases where a test fails to deliver the expected outcome, it is important to do error handling and descriptive logging.

## Running Locally

1. Open a powershell window in `dxvk-remix-nv/` and run `.\build_dxvk.ps1 -BuildFlavour release -BuildSubDir _Comp64UnitTest -Backend ninja -EnableTracy false -BuildTarget unit_tests -InstallTags unit`
2. Then change directory into `_Comp64UnitTest/` and run `meson test --verbose <test_name>`.  Leave off the test name to run all tests.

Note: If the above steps don't work, check `tools/ci/gitlab/unit_test.yml` to see if the commands have changed.