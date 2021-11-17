#include <iostream>
#include <chrono>
#include <vector>
#include <thread> 
#include <atomic>
#include <random>

#include <getopt.h>
#include <string.h>

#include "sequential.cpp"
#include "concurrent.cpp"

enum implementation_t {
    sequential = 1,
    concurrent = 2,
    transactional = 3
};

struct config {

    // Maximum key size
    int range;

    // Initial table size
    int size;

    // Number of items to populate the table with
    int population;

    // Number of operations to run
    int operations;

    // The number of threads for which a test should run
    int threads;

    // Seed for the random number generator
    int seed;

    // Number of locks to use for concurrent striping implementations
    int locks;

    // Imlementation to run (sequential, concurrent, transactional)
    implementation_t implementation;

    config() {
        range = INT32_MAX;
        size = pow(2, 21);
        population = pow(2, 20);
        operations = 1000000;
        threads = 1;
        seed = rand();
        locks = (size / 8);
        implementation = sequential;
    }
};

struct results {
    std::atomic<int> add_true;
    std::atomic<int> add_false;

    std::atomic<int> remove_true;
    std::atomic<int> remove_false;

    std::atomic<int> contains_true;
    std::atomic<int> contains_false;

    results() {
        contains_true = 0;
        contains_false = 0;

        add_true = 0;
        add_false = 0;

        remove_true = 0;
        remove_false = 0;
    }
};

void parseargs(int argc, char** argv, config& cfg) {
    int opt;
    while ((opt = getopt(argc, argv, "r:s:p:o:t:x:l:i")) != -1) {
        switch (opt) {
            case 'r': cfg.range = atoi(optarg); break;
            case 's': cfg.size = atoi(optarg); break;
            case 'p': cfg.population = atoi(optarg); break;
            case 'o': cfg.operations = atoi(optarg); break;
            case 't': cfg.threads = atoi(optarg); break;
            case 'x': cfg.seed = atoi(optarg); break;
            case 'l': cfg.locks = atoi(optarg); break;
            case 'i':
                if (!strcmp(optarg, "sequential")) {
                    cfg.implementation = sequential;
                }
                else if (!strcmp(optarg, "concurrent")) {
                    cfg.implementation = concurrent;
                }
                else if (!strcmp(optarg, "transactional")) {
                    cfg.implementation = transactional;
                }
                else {
                    std::cout << "Available implementations are: 'sequential', 'concurrent', or 'transactional'" << std::endl;
                    exit(1);
                }; 
                break;
        }
    }
}

// Shared RNG
std::default_random_engine generator;

// Uniform distributions for both the value and operation workload generation
std::uniform_int_distribution<int> value_distribution;
std::uniform_int_distribution<int> operation_distribution;


std::vector<char> op_distribution(config cfg) {
	std::vector<char> dist;

	int opcount = 2 * (cfg.operations / cfg.threads);
	for (int i = 0; i < opcount; ++i) {
		int op = operation_distribution(generator);

		if (op < 10) {
			dist.push_back('a');
		} else if (op < 20) {
			dist.push_back('r');
		} else if (op < 100) {
			dist.push_back('c');
		}
	}

	return dist;
}

std::vector<int> val_distribution(config cfg) {
	std::vector<int> dist;

	int opcount = 2 * (cfg.operations / cfg.threads);
	for (int i = 0; i < opcount; ++i) {
		int op = value_distribution(generator);

		dist.push_back(op);
	}
    
	return dist;
}

int random_int() {
    return value_distribution(generator);
}

std::atomic<int> total_operations;

void do_work(set<int>* int_set, results &res, config cfg) {

    std::vector<char> op_dist = op_distribution(cfg);
	std::vector<int> val_dist = val_distribution(cfg);

	auto op_iter = op_dist.begin();
	auto val_iter = val_dist.begin();

    int add_true = 0;
    int add_false = 0;

    int remove_true = 0;
    int remove_false = 0;

    int contains_true = 0;
    int contains_false = 0;

	while(++total_operations < cfg.operations + 1) {
		switch (*op_iter) {
			case 'a':
			{
                if(int_set->add(*val_iter)) {
                    add_true++;
                } else {
                    add_false++;
                }

				break;
			}

			case 'r':
			{
                if(int_set->remove(*val_iter)) {
                    remove_true++;
                } else {
                    remove_false++;
                }
                
				break;
			}

            case 'c':
			{
                if(int_set->contains(*val_iter)) {
                    contains_true++;
                } else {
                    contains_false++;
                }
                
				break;
			}
			
			default:
				break;
		}

        val_iter++;
		op_iter++;
	}

    res.add_true += add_true;
    res.add_false += add_false;

    res.remove_true += remove_true;
    res.remove_false += remove_false;

    res.contains_true += contains_true;
    res.contains_false += contains_false;

    return;
}

int main(int argc, char** argv) {

    const int limit = 1000;

    config cfg;
    parseargs(argc, argv, cfg);

    std::cout << std::endl << ".____________." << std::endl;
    std::cout << "|            |" << std::endl;
    std::cout << "| Parameters |" << std::endl;
    std::cout << "|____________|" << std::endl << std::endl;
    std::cout << "[implementation]: " << cfg.implementation << std::endl;
    std::cout << "[range]:          " << cfg.range << std::endl;
    std::cout << "[size]:           " << cfg.size << std::endl;
    std::cout << "[population]:     " << cfg.population << std::endl;
    std::cout << "[operations]:     " << cfg.operations << std::endl;
    std::cout << "[threads]:        " << cfg.threads << std::endl;
    std::cout << "[seed]:           " << cfg.seed << std::endl << std::endl;

    set<int>* int_set = NULL;

    generator = std::default_random_engine(cfg.seed);
    value_distribution = std::uniform_int_distribution<int>(0, cfg.range);
    operation_distribution = std::uniform_int_distribution<int>(0, 99);

    switch(cfg.implementation){
        case sequential:
            int_set = new sequential_set<int>(cfg.size, limit);
            cfg.threads = 1;
            break;
        case concurrent:
            int_set = new concurrent_set<int>(cfg.size, cfg.locks, limit);
            break;
        default:
            break;
    }

    std::vector<std::thread> threads;
    results res;

    std::cout << "pre " << int_set->size() << std::endl;

    int_set->populate(cfg.population, &random_int);

    std::cout << "post " << int_set->size() << std::endl << std::flush;


    auto start = std::chrono::high_resolution_clock::now();

	if (cfg.threads == 1) {
		do_work(int_set, res, cfg);
	} else {
		for (int i = 0; i < cfg.threads; ++i) {
			threads.push_back(std::thread(&do_work, int_set, std::ref(res), cfg));
		}

		for (int i = 0; i < cfg.threads; ++i) {
			threads.at(i).join();
		}
	}

    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::microseconds elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    auto time = elapsed.count();

    int set_size = int_set->size();

    // Print the results
    std::cout << "._________." << std::endl;
    std::cout << "|         |" << std::endl;
    std::cout << "| Results |" << std::endl;
    std::cout << "|_________|" << std::endl << std::endl;

    std::cout << "[add_true]:           " << res.add_true << std::endl;
    std::cout << "[add_false]:          " << res.add_false << std::endl << std::endl;

    std::cout << "[remove_true]:        " << res.remove_true << std::endl;
    std::cout << "[remove_false]:       " << res.remove_false << std::endl << std::endl;

    std::cout << "[contains_true]:      " << res.contains_true << std::endl;
    std::cout << "[contains_false]:     " << res.contains_false << std::endl << std::endl;

    std::cout << "[total_operations]:   " << res.add_true + res.add_false + res.remove_true + res.remove_false + res.contains_true + res.contains_false << std::endl << std::endl;

    std::cout << "[expected_size]:      " << cfg.population + res.add_true - res.remove_true << std::endl;
    std::cout << "[actual_size]:        " << set_size << std::endl << std::endl;

    std::cout << "[execution_time]:     " << time << std::endl;

    delete int_set;
    return 0;
}
