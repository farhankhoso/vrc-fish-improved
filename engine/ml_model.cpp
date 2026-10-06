#include "ml_model.h"

#include <fstream>
#include <sstream>
#include <utility>

namespace engine {

namespace {

void assignError(std::string* error, const std::string& message) {
	if (error != nullptr) {
		*error = message;
	}
}

void pushNote(std::vector<std::string>* notes, const std::string& message) {
	if (notes != nullptr) {
		notes->push_back(message);
	}
}

}  // namespace

bool loadMlpWeights(
	const std::string& path,
	MlpModel& model,
	std::string* error,
	std::vector<std::string>* notes) {
	std::ifstream f(path);
	if (!f.is_open()) {
		assignError(error, "[ML] failed to open weights file: " + path);
		return false;
	}

	model.layers.clear();
	std::string line;
	while (std::getline(f, line)) {
		if (line.empty() || line[0] == '#') {
			continue;
		}

		std::istringstream hdr(line);
		int in_dim = 0;
		int out_dim = 0;
		if (!(hdr >> in_dim >> out_dim)) {
			continue;
		}

		MlpLayer layer;
		layer.in_dim = in_dim;
		layer.out_dim = out_dim;
		layer.weights.resize(static_cast<size_t>(out_dim) * static_cast<size_t>(in_dim));
		layer.bias.resize(static_cast<size_t>(out_dim));

		for (int r = 0; r < out_dim; r++) {
			if (!std::getline(f, line)) {
				assignError(error, "[ML] invalid weights format (weights section)");
				return false;
			}
			std::istringstream row(line);
			for (int c = 0; c < in_dim; c++) {
				if (!(row >> layer.weights[static_cast<size_t>(r) * static_cast<size_t>(in_dim) + static_cast<size_t>(c)])) {
					assignError(error, "[ML] invalid weights format (weight value)");
					return false;
				}
			}
		}

		if (!std::getline(f, line)) {
			assignError(error, "[ML] invalid weights format (bias section)");
			return false;
		}
		std::istringstream brow(line);
		for (int r = 0; r < out_dim; r++) {
			if (!(brow >> layer.bias[static_cast<size_t>(r)])) {
				assignError(error, "[ML] invalid weights format (bias value)");
				return false;
			}
		}

		model.layers.push_back(std::move(layer));
	}

	if (model.layers.empty()) {
		assignError(error, "[ML] weights file is empty");
		return false;
	}

	model.loaded = true;
	pushNote(notes, "[ML] model loaded: layers=" + std::to_string(model.layers.size()));

	std::string normPath = path;
	size_t dotPos = normPath.rfind('.');
	if (dotPos != std::string::npos) {
		normPath = normPath.substr(0, dotPos) + "_norm.txt";
	} else {
		normPath += "_norm";
	}

	std::ifstream nf(normPath);
	if (nf.is_open()) {
		std::string nline;
		model.norm_mean.clear();
		model.norm_std.clear();
		while (std::getline(nf, nline)) {
			if (nline.empty() || nline[0] == '#') {
				continue;
			}
			std::istringstream ns(nline);
			double m = 0.0;
			double s = 0.0;
			if (ns >> m >> s) {
				model.norm_mean.push_back(m);
				model.norm_std.push_back(s);
			}
		}
		pushNote(notes, "[ML] normalization loaded: features=" + std::to_string(model.norm_mean.size()));
	} else {
		pushNote(notes, "[ML] warning: normalization file not found: " + normPath + ", using raw input");
	}

	return true;
}

}  // namespace engine
