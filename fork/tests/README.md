# Comments data tests

These tests use Qt Core and the production comment parser and pagination code.
They cover overlapping pages, pin order, 64-bit IDs, replies to replies, author
fallbacks, permissions and Unicode. They make no network requests.

```sh
cmake -S fork/tests -B out-comments-tests -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build out-comments-tests
ctest --test-dir out-comments-tests --output-on-failure
```

Passing a filename to `comments_data_tests` also writes the five GraphQL
operations as JSON for validation against the service schema.
