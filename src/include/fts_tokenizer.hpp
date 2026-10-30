#pragma once

#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

ScalarFunction GetFTSTokenizeFunction();
ScalarFunction GetFTSTokenizeSpansFunction();

} // namespace duckdb
