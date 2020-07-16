#pragma once
#ifndef SAF_ETW
#define SAF_ETW

#include "exprtk.hpp"

#include <array>
#include <vector>

struct exprtk_wrapper{
	typedef double T;
	typedef exprtk::symbol_table<T> symbol_table_t;
	typedef exprtk::expression<T>     expression_t;
	typedef exprtk::parser<T>             parser_t;
	typedef exprtk::parser_error::type       err_t;

	std::string expr;
	std::array<double, 26> variables;
	symbol_table_t symbol_table;
	expression_t expression;
	parser_t parser;
	exprtk_wrapper(std::vector<std::pair<std::string, double&>> ext_variables) {
		std::string str = "a";
		for (auto& variable : variables) {
			symbol_table.add_variable(str, variable);
			str[0]++;
		}
		for (auto& var : ext_variables) {
			auto& [str, val] = var;
			symbol_table.add_variable(str, val);
		}
		expression.register_symbol_table(symbol_table);
	}
	bool compile(const std::string& expression_string) {
		return parser.compile(expression_string, expression);
	}
	auto get_errors() {
		std::vector<std::tuple<size_t, size_t, std::string, std::string>> errors;
		size_t errors_count = parser.error_count();
		for (std::size_t i = 0; i < errors_count; ++i) {
			err_t error = parser.get_error(i);

			errors.push_back({
				i,
				error.token.position,
				exprtk::parser_error::to_str(error.mode),
				error.diagnostic
			});
		}
		return errors;
	}
	double evalute() {
		return expression.value();
	}
};

#endif // !SAF_ETW

