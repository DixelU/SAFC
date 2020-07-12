#pragma once

#ifndef SAF_ETE
#define SAF_ETE

#include "tinyexpr.h"
#include <vector>
#include <deque>
#include <string>
#include <algorithm>
#include <array>
#include <utility>
#include <boost/algorithm/string.hpp>

struct extended_tinyexpr {
	std::array<double, 26> _inner_variables_holder;
	std::vector<te_variable> variables;
	std::vector<te_expr*> compiled_expr;
	std::vector<unsigned char> ce_result_id;

	extended_tinyexpr(const std::vector<te_variable>& vars) {
		char c = 0;
		for (auto& el : _inner_variables_holder) {
			auto ptr = new char[2];
			ptr[0] = 'A' + (c++);
			ptr[1] = '\0';
			variables.push_back(te_variable{ ptr , &el });
		}
		for (const auto& el : vars) 
			variables.push_back(el);
	}
	void clear() {
		for (auto& te : compiled_expr)
			te_free(te);
		compiled_expr.clear();
		ce_result_id.clear();
		//variables.clear();
	}
	~extended_tinyexpr() {
		clear();
	}
	static bool is_empty(const std::string& str) {
		for (auto& ch : str) {
			switch (ch)	{
			case ' ':
			case '\n':
			case '\t':
				break;
			default:
				return false;
			}
		}
		return true;
	}
	std::vector<std::pair<size_t, int>> /*char pos, error no*/ recompile(std::string code) {
		clear();
		std::replace(code.begin(), code.end(), '\n', ' ');
		std::vector<std::string> lines;
		std::vector<std::pair<size_t, int>> errors;
		boost::algorithm::split(lines, code, boost::is_any_of(";"));
		auto Y = lines.begin();
		while (Y != lines.end()) {
			if (is_empty(*Y))
				Y = lines.erase(Y);
			else
				Y++;
		}
		int error;
		size_t line_no = 0;
		const std::string local_assign_command = "#ASSIGN_";
		for (auto& line : lines) {
			error = 0;
			if (line.substr(0, local_assign_command.size()) == local_assign_command && line.size() > local_assign_command.size()) {
				char var = line[local_assign_command.size()] - 'A';
				if (var < 0 || var > 26)
					continue;
				line = line.substr(local_assign_command.size(), 0x7FFFFFFF);
				ce_result_id.push_back(var);
			}
			else 
				ce_result_id.push_back(127);
			compiled_expr.push_back(te_compile(line.c_str(), variables.data(), variables.size(), &error));
			if (error)
				errors.push_back({ line_no, error });
			line_no++;
		}
		return std::move(errors);
	}
	inline double get_last_expression_value() {
		double value = 0;
		for (int i = 0; i < ce_result_id.size(); i++) {
			value = te_eval(compiled_expr[i]);
			if (ce_result_id[i] < 27)
				_inner_variables_holder[ce_result_id[i]] = value;
		}
		return value;
	}
};

#endif