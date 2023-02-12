// Some code from https://github.com/zbycz/srtm-hgt-reader

#include "hgt.h" //fmod

#include <fstream>
#include <ios>
#include <filesystem>
#include <iostream>
#include <math.h>
#include <string>

hgt::hgt(const std::string &folder) : folder{folder}
{
}

bool hgt::load(int lat_dec, int lon_dec)
{
	if (lat_loaded == lat_dec && lon_loaded == lon_dec) {
		return false;
	}

	// auto lock = std::unique_lock(mutex);

	std::string filename(255, 0);

	sprintf(filename.data(), "%s/%c%02d%c%03d.hgt", folder.c_str(),
			lat_dec > 0 ? 'N' : 'S', abs(lat_dec), lon_dec > 0 ? 'E' : 'W', abs(lon_dec));
	if (!std::filesystem::exists(filename)) {
		std::cerr << "Missing file " << filename << " for " << lat_dec << "," << lon_dec
				  << std::endl;
		return true;
	}
	auto filesize = std::filesystem::file_size(filename);
	if (filesize == 2884802)
		seconds_per_px = 3;
	else if (filesize == 25934402)
		seconds_per_px = 1;
	else
		throw std::logic_error("unknown file size " + std::to_string(filesize));

	side_length = sqrt(filesize >> 1);

	std::ifstream istrm(filename, std::ios::binary);

	if (!istrm.good()) {
		std::cerr << "Error opening " << filename << std::endl;
		return true;
	}

	uint8_t srtmTile[filesize];
	istrm.read(reinterpret_cast<char *>(srtmTile), filesize);
	heights.resize(filesize >> 1);
	for (uint32_t i = 0; i < filesize >> 1; ++i) {
		int16_t height = (srtmTile[i << 1] << 8) | (srtmTile[(i << 1) + 1]);
		if (height == -32768) {
			height = 0;
		}
		heights[i] = height;
	}
	lat_loaded = lat_dec;
	lon_loaded = lon_dec;

	return false;
}

/** Pixel idx from left bottom corner (0-1200) */
int16_t hgt::read(int16_t y, int16_t x)
{
	const int row = (side_length - 1) - y;
	const int col = x;
	const int pos = (row * side_length + col);
	return heights[pos];
}

float hgt::get(ll_t lat, ll_t lon)
{
	const int lat_dec = (int)floor(lat);
	const int lon_dec = (int)floor(lon);

	const ll_t lat_seconds = (lat - (ll_t)lat_dec) * 60 * 60;
	const  ll_t lon_seconds = (lon - (ll_t)lon_dec) * 60 * 60;

	if (load(lat_dec, lon_dec)) {
		return 0;
	}

	const int y = lat_seconds / seconds_per_px;
	const int x = lon_seconds / seconds_per_px;

	const int16_t height[] = {
			read(y + 1, x),
			read(y + 1, x + 1),
			read(y, x),
			read(y, x + 1),
	};
	// return height[2]; // debug not interpolated

	// ratio where X lays
	const float dy = fmod(lat_seconds, seconds_per_px) / seconds_per_px;
	const float dx = fmod(lon_seconds, seconds_per_px) / seconds_per_px;

	// Bilinear interpolation
	// h0------------h1
	// |
	// |--dx-- .
	// |       |
	// |      dy
	// |       |
	// h2------------h3
	return height[0] * dy * (1 - dx) + height[1] * dy * (dx) +
		   height[2] * (1 - dy) * (1 - dx) + height[3] * (1 - dy) * dx;
}
