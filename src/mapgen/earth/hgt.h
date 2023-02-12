
#pragma once

#include <string>
#include <vector>
class hgt
{
	using ll_t = float;
	uint16_t side_length = 0; // = 3601;
	uint8_t seconds_per_px = 0;
	const std::string folder; // = "/home/proller/games/freeminer_p3/earth";
	int lat_loaded = 200;
	int lon_loaded = 200;
	// std::mutex mutex; // TODO
	std::vector<int16_t> heights;

	bool load(int lat_dec, int lon_dec);
	int16_t read(int16_t y, int16_t x);

public:
	hgt(const std::string &folder);
	float get(ll_t lat, ll_t lon);
};
