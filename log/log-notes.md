Logging layer: log macros, log scopes, and the hooks that make assert and
arena failures print. Sits above the upper os layer.

Files:
- log.h / log.cpp: the whole layer (log.cpp is also the unity file).
- test/log_test.cpp: `fbuild log-test-debug`. Includes base.cpp, so it also
  checks the full unity build compiles in order.
