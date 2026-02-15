// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Cline AI Assistant

#include "catch.h"
#include "database/database-dummy.h"
#include "database/database-leveldb.h"
#include "database/database-sqlite3.h"
#include "database/database-postgresql.h"
#include "database/database-redis.h"
#include "irr_v3d.h"
#include "porting.h"
#include "filesys.h"
#include "settings.h"
#include <exception>
#include <memory>
#include <random>
#include <algorithm>

namespace
{

static bool g_benchmark_initialized = false;
static std::string g_test_directory;

static void initialize_benchmark_environment()
{
	if (g_benchmark_initialized)
		return;

	// Create temporary directory for benchmark databases
	g_test_directory = porting::path_share + DIR_DELIM + ".benchmark_tmp";
	fs::CreateAllDirs(g_test_directory);

	g_benchmark_initialized = true;
}

static void cleanup_benchmark_environment()
{
	if (!g_benchmark_initialized)
		return;

	// Clean up temporary directory
	fs::RecursiveDelete(g_test_directory);

	g_benchmark_initialized = false;
}

static std::unique_ptr<MapDatabase> create_dummy_database()
{
	return std::make_unique<Database_Dummy>();
}

static std::unique_ptr<MapDatabase> create_leveldb_database()
{
	// Check if LevelDB is enabled
#if !USE_LEVELDB
	return nullptr;
#else
	std::string db_path = g_test_directory + DIR_DELIM + "leveldb_test";
	fs::CreateAllDirs(db_path);
	return std::make_unique<Database_LevelDB>(db_path);
#endif
}

static std::unique_ptr<MapDatabase> create_sqlite3_database()
{
#if !USE_SQLITE3
	return nullptr;
#else
	std::string db_path = g_test_directory + DIR_DELIM + "sqlite3_test";
	fs::CreateAllDirs(db_path);
	return std::make_unique<MapDatabaseSQLite3>(db_path);
#endif
}

static std::unique_ptr<MapDatabase> create_postgresql_database()
{
#if !USE_POSTGRESQL
	return nullptr;
#else
	try {
		/*
		sudo -u postgres createuser -P test
		sudo -u postgres createdb --owner=test test
		echo "localhost:5432:test:test:pass" >> ~/.pgpass
		chmod 0600 ~/.pgpass
		*/
		std::unique_ptr<MapDatabase> db = std::make_unique<MapDatabasePostgreSQL>(
				"host=localhost dbname=test user=test");
		if (!db->initialized()) {
			return nullptr;
		}
		return db;
	} catch (const std::exception &ex) {
		std::cerr << ex.what() << "\n";
		return nullptr;
	}
#endif
}

static std::unique_ptr<MapDatabase> create_redis_database()
{
#if !USE_REDIS
	return nullptr;
#else
	Settings conf;
	conf.set("redis_address", "localhost");
	conf.set("redis_hash", "test");
	try {
		std::unique_ptr<MapDatabase> db = std::make_unique<Database_Redis>(conf);
		return db;
	} catch (const std::exception &ex) {
		std::cerr << ex.what() << "\n";
		return nullptr;
	}
#endif
}

// Generate test data
static std::string generateTestData(size_t size = 1024)
{
	std::string data;
	data.reserve(size);

	// Generate pseudo-random printable ASCII data for consistent testing
	std::mt19937 gen(42);						  // Fixed seed for reproducible results
	std::uniform_int_distribution<> dis(32, 126); // Printable ASCII range

	for (size_t i = 0; i < size; ++i) {
		data += static_cast<char>(dis(gen));
	}

	return data;
}

static v3pos_t generateTestPosition(int index)
{
	// Generate deterministic positions for reproducible benchmarks
	// Use a simple hash-like function to distribute positions
	int x = (index * 1103515245 + 12345) & 0x7FFFFFFF;
	int y = (index * 1103515247 + 54321) & 0x7FFFFFFF;
	int z = (index * 1103515249 + 98765) & 0x7FFFFFFF;

	// Scale down and center around origin
	x = (x % 1000) - 500;
	y = (y % 1000) - 500;
	z = (z % 1000) - 500;

	return v3pos_t(x, y, z);
}

// Benchmark functions for each operation
template <typename DatabaseFactory>
static void benchmarkSaveBlock(
		DatabaseFactory factory, const std::string &db_name, size_t iterations)
{
	BENCHMARK_ADVANCED("SaveBlock_" + db_name)(Catch::Benchmark::Chronometer meter)
	{
		initialize_benchmark_environment();
		auto db = factory();
		if (!db) {
			meter.measure([] { return 0; }); // Skip if database not available
			return;
		}

		std::string test_data = generateTestData();
		db->beginSave();

		meter.measure([&] {
			for (size_t i = 0; i < meter.runs(); ++i) {
				const auto pos = generateTestPosition(i);
				db->saveBlock(pos, test_data);
			}
			return meter.runs();
		});

		db->endSave();
	};
}

template <typename DatabaseFactory>
static void benchmarkLoadBlock(
		DatabaseFactory factory, const std::string &db_name, size_t iterations)
{
	BENCHMARK_ADVANCED("LoadBlock_" + db_name)(Catch::Benchmark::Chronometer meter)
	{
		initialize_benchmark_environment();
		auto db = factory();
		if (!db) {
			meter.measure([] { return 0; }); // Skip if database not available
			return;
		}

		// Pre-populate database
		std::string test_data = generateTestData();
		db->beginSave();
		for (size_t i = 0; i < iterations; ++i) {
			const auto pos = generateTestPosition(i);
			db->saveBlock(pos, test_data);
		}
		db->endSave();

		std::string loaded_data;
		meter.measure([&] {
			for (size_t i = 0; i < meter.runs(); ++i) {
				const auto pos = generateTestPosition(i % iterations);
				db->loadBlock(pos, &loaded_data);
			}
			return meter.runs();
		});
	};
}

template <typename DatabaseFactory>
static void benchmarkDeleteBlock(
		DatabaseFactory factory, const std::string &db_name, size_t iterations)
{
	BENCHMARK_ADVANCED("DeleteBlock_" + db_name)(Catch::Benchmark::Chronometer meter)
	{
		initialize_benchmark_environment();
		auto db = factory();
		if (!db) {
			meter.measure([] { return 0; }); // Skip if database not available
			return;
		}

		// Pre-populate database
		std::string test_data = generateTestData();
		db->beginSave();
		for (size_t i = 0; i < iterations; ++i) {
			auto pos = generateTestPosition(i);
			db->saveBlock(pos, test_data);
		}
		db->endSave();

		meter.measure([&] {
			size_t deleted = 0;
			for (size_t i = 0; i < meter.runs(); ++i) {
				auto pos = generateTestPosition(i % iterations);
				if (db->deleteBlock(pos)) {
					deleted++;
				}
			}
			return deleted;
		});
	};
}

template <typename DatabaseFactory>
static void benchmarkListAllBlocks(
		DatabaseFactory factory, const std::string &db_name, size_t iterations)
{
	BENCHMARK_ADVANCED("ListAllBlocks_" + db_name)(Catch::Benchmark::Chronometer meter)
	{
		initialize_benchmark_environment();
		auto db = factory();
		if (!db) {
			meter.measure([] { return 0; }); // Skip if database not available
			return;
		}

		// Pre-populate database with reasonable amount of data
		size_t populate_count = std::min(iterations, static_cast<size_t>(1000));
		std::string test_data = generateTestData();
		db->beginSave();
		for (size_t i = 0; i < populate_count; ++i) {
			const auto pos = generateTestPosition(i);
			db->saveBlock(pos, test_data);
		}
		db->endSave();

		std::vector<v3pos_t> block_list;
		meter.measure([&] {
			size_t total_found = 0;
			for (size_t i = 0; i < meter.runs(); ++i) {
				block_list.clear();
				db->listAllLoadableBlocks(block_list);
				total_found += block_list.size();
			}
			return total_found;
		});
	};
}

} // namespace

TEST_CASE("benchmark_database_operations")
{
	const auto iterations1 = 10000;
	const auto iterations2 = 1000;

	const auto have_postgresl = !!create_postgresql_database();
	const auto have_redis = !!create_redis_database();
	SECTION("SaveBlock Operations")
	{

		benchmarkSaveBlock(create_dummy_database, "Dummy", iterations1);
		benchmarkSaveBlock(create_leveldb_database, "LevelDB", iterations1);
		benchmarkSaveBlock(create_sqlite3_database, "SQLite3", iterations1);
		if (have_postgresl)
			benchmarkSaveBlock(create_postgresql_database, "Postgresql", iterations1);
		if (have_redis)
			benchmarkSaveBlock(create_redis_database, "Redis", iterations1);
	}

	SECTION("LoadBlock Operations")
	{
		benchmarkLoadBlock(create_dummy_database, "Dummy", iterations1);
		benchmarkLoadBlock(create_leveldb_database, "LevelDB", iterations1);
		benchmarkLoadBlock(create_sqlite3_database, "SQLite3", iterations1);
		if (have_postgresl)
			benchmarkLoadBlock(create_postgresql_database, "Postgresql", iterations1);
		if (have_redis)
			benchmarkLoadBlock(create_redis_database, "Redis", iterations1);
	}

	SECTION("DeleteBlock Operations")
	{
		benchmarkDeleteBlock(create_dummy_database, "Dummy", iterations1);
		benchmarkDeleteBlock(create_leveldb_database, "LevelDB", iterations1);
		benchmarkDeleteBlock(create_sqlite3_database, "SQLite3", iterations1);
		if (have_postgresl)
			benchmarkDeleteBlock(create_postgresql_database, "Postgresql", iterations1);
		if (have_redis)
			benchmarkDeleteBlock(create_redis_database, "Redis", iterations1);
	}

	SECTION("ListAllBlocks Operations")
	{
		benchmarkListAllBlocks(create_dummy_database, "Dummy", iterations2);
		benchmarkListAllBlocks(create_leveldb_database, "LevelDB", iterations2);
		benchmarkListAllBlocks(create_sqlite3_database, "SQLite3", iterations2);
		if (have_postgresl)
			benchmarkListAllBlocks(create_postgresql_database, "Postgresql", iterations2);
		if (have_redis)
			benchmarkListAllBlocks(create_redis_database, "Redis", iterations2);
	}
}

// Cleanup at the end
TEST_CASE("benchmark_database_cleanup")
{
	cleanup_benchmark_environment();
}
