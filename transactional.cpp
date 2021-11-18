#include <vector>
#include <functional>
#include <iostream>
#include <shared_mutex>
#include <mutex>

#include "set.h"

template <typename T> class transactional_set: public set<T> {

    // Hashset entry holds both the value and a flag to determine if the entry currently holds a value. By default this flag is false.
    struct entry {
        T value;
        bool has_value;
    };

    private:

        // Current size of the hashset
        int set_size;

        // The maximum amount of tries we should attempt before resizing the table
        int limit;

        int probe_size = 4;
        int threshold = 2;

        // Tables which correspond to their appropriate hash functions
        std::vector<std::vector<T>> tables[2];

        // Primary table
        int hash0(int value) {
            return value % set_size;
        }

        // Secondary table, same function plus an offset
        int hash1(int value) {
            uint x = value;
            x = ((x >> 16) ^ x) * 0x45d9f3b;
            x = ((x >> 16) ^ x) * 0x45d9f3b;
            x = (x >> 16) ^ x;
            return x % set_size;
        }       

        __attribute__ ((transaction_pure))
        void resize() {
            __transaction_atomic {
                // Track the old size, double the current
                int size_old = set_size;

                if (size_old != set_size) {
                    // We didn't acquire locks in time
                    return;
                }

                std::vector<std::vector<T>> table0_old = tables[0];
                std::vector<std::vector<T>> table1_old = tables[1];

                set_size = size_old * 2;

                tables[0] = std::vector<std::vector<T>>(set_size);
                tables[1] = std::vector<std::vector<T>>(set_size);

                for(int i = 0; i < set_size; i++) {
                    tables[0].push_back(std::vector<T>(probe_size));
                    tables[1].push_back(std::vector<T>(probe_size));
                }

                // Copy over the old entries, but only the ones that had values
                for(int i = 0; i < size_old; i++) {
                    for (auto it = table0_old[i].begin(); it != table0_old[i].end(); ++it) {
                        add(*it);
                    }

                    for (auto it = table1_old[i].begin(); it != table1_old[i].end(); ++it) {
                        add(*it);
                    }
                }
            }
        }

        __attribute__ ((transaction_pure))
        bool relocate(int i, int hi) {
            __transaction_atomic {
                int j = 1 - i;
                int hj = 0;

                for (int round = 0; round < limit; round++) {
                    T val = tables[i][hi].at(0);

                    if (i) {
                        hj = hash0(val);
                    }
                    else {
                        hj = hash1(val);
                    }

                    bool removed;
                    for (int i = 0; i < tables[i][hi].size(); ++i) {
                        if (tables[i][hi].at(i) == val) {
                            tables[i][hi].erase(tables[i][hi].begin() + i);
                            removed = true;
                            break;
                        }
                    }

                    if (removed) {
                        if (tables[j][hj].size() < threshold) {
                            tables[j][hj].push_back(val);
                            return true;
                        }
                        else if (tables[j][hj].size() < probe_size) {
                            tables[j][hj].push_back(val);
                            i = 1 - i;
                            hi = hj;
                            j = 1 - j;
                        }
                        else {
                            tables[i][hi].push_back(val);
                            return false;
                        }
                    }
                    else if (tables[i][hi].size() >= threshold) {
                        continue;
                    }
                    else {
                        return true;
                    }
                }

                return false;
            }

        }

        // Swap a new entry, return the old one
        entry swap(entry* table, T value, int index) {
            entry entry_old = table[index];

            table[index].value = value;
            table[index].has_value = true;

            return entry_old;
        }

    public:

        transactional_set(int size, int limit) {
            this->set_size = size;
            this->limit = limit;

            tables[0] = std::vector<std::vector<T>>(size);
            tables[1] = std::vector<std::vector<T>>(size);

            for(int i = 0; i < set_size; i++) {
                tables[0].push_back(std::vector<T>(probe_size));
                tables[1].push_back(std::vector<T>(probe_size));
            }
        }

        __attribute__ ((transaction_pure))
        bool add(T value) {
            __transaction_atomic {
                // If the table already contains the value return false
                if (contains(value)) {
                    return false;
                }

                int index0 = hash0(value);
                int index1 = hash1(value);

                bool to_resize = false;

                int table_index = -1;
                int hash_index = -1;

                if (tables[0][index0].size() < threshold) {
                    tables[0][index0].push_back(value);
                    return true;
                }
                else if (tables[1][index1].size() < threshold) {
                    tables[1][index1].push_back(value);
                    return true;
                }
                else if (tables[0][index0].size() < probe_size) {
                    tables[0][index0].push_back(value);
                    table_index = 0;
                    hash_index = index0;
                }
                else if (tables[1][index1].size() < probe_size) {
                    tables[1][index1].push_back(value);
                    table_index = 1;
                    hash_index = index1;
                }
                else {
                    to_resize = true;
                }
                
                if (to_resize) {
                    resize();
                    add(value);
                }
                else if (!relocate(table_index, hash_index)) {
                    resize();
                }

                return true;
            }
        }

        __attribute__ ((transaction_pure))
        bool remove(T value){
            __transaction_atomic {
                // Check if the value is in table0, if so, remove it
                int index0 = hash0(value);

                for (auto it = tables[0][index0].begin(); it != tables[0][index0].end(); ++it) {
                    if (*it == value) {
                        tables[0][index0].erase(it);
                        return true;
                    }
                }

                // Perform the same check for table1
                int index1 = hash1(value);
                
                for (auto it = tables[1][index1].begin(); it != tables[1][index1].end(); ++it) {
                    if (*it == value) {
                        tables[1][index1].erase(it);
                        return true;
                    }
                }
                
                return false;
            }
        }

        __attribute__ ((transaction_pure))
        bool contains(T value){
            __transaction_atomic {
                // Check if the value is in table0
                int index0 = hash0(value);

                for (auto it = tables[0][index0].begin(); it != tables[0][index0].end(); ++it) {
                    if (*it == value) {
                        return true;
                    }
                }

                // Perform the same check for table1
                int index1 = hash1(value);

                for (auto it = tables[1][index1].begin(); it != tables[1][index1].end(); ++it) {
                    if (*it == value) {
                        return true;
                    }
                }
                
                return false;
            }
        }

        int size() {
            int count = 0;
            
            // Iterate over both tables, add to count whenever we hit an element
            for(int i = 0; i < set_size; i++) {
                count += tables[0][i].size();

                count += tables[1][i].size();
            }

            return count;
        }

        // Generate random values until we've inserted pop items
        void populate(int pop, T (*random_t)()) {
            for(int i = 0; i < pop; i++) {
                while(!add(random_t()));
            }
        }
};