#!/bin/bash
env ASAN_OPTIONS='abort_on_error=1' gdb cmantic
