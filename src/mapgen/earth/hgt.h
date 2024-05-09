
#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class hgt
{
public:
	using ll_t = float;

private:
	uint16_t side_length = 0; // = 3601;
	uint8_t seconds_per_px = 0;
	const std::string folder;
	int lat_loaded = 200;
	int lon_loaded = 200;
	int lat_loading = 200;
	int lon_loading = 200;
	std::mutex mutex; //, mutex2;
	std::vector<int16_t> heights;

	bool load(int lat_dec, int lon_dec);
	int16_t read(int16_t y, int16_t x);

public:
	hgt(const std::string &folder, int lat_dec, int lon_dec);
	float get(ll_t lat, ll_t lon);
};

class hgts
{
	std::map<int, std::map<int, std::optional<hgt>>> map;
	const std::string folder;
	std::mutex mutex;

public:
	hgts(const std::string &folder);
	hgt *get(hgt::ll_t lat, hgt::ll_t lon);
};