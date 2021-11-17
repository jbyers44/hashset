#include <vector>
#include <functional>
#include <iostream>
#include <shared_mutex>
#include <mutex>

#include "set.h"

template <typename T> class concurrent_set: public set<T> {

    // Hashset entry holds both the value and a flag to determine if the entry currently holds a value. By default this flag is false.
    struct entry {
        T value;
        bool has_value;
    };

    private:

        // Current size of the hashset
        int set_size;

        // Offset amount for hash1
        int offset;

        // The maximum amount of tries we should attempt before resizing the table
        int limit;

        // Tables which correspond to their appropriate hash functions
        entry* table0;
        entry* table1;

        std::vector<std::shared_mutex> lock_table0;
        std::vector<std::shared_mutex> lock_table1;

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

        void resize() {
            // Track the old size, double the current
            int size_old = set_size;

            set_size = (size_old * 2);

            // Keep track of the old data as we'll need to reinsert it with the "new" hash function
            entry* table0_old = table0;
            entry* table1_old = table1;

            // New tables, default initializes has_value to false
            table0 = new entry[set_size];
            table1 = new entry[set_size];

            for(int i = 0; i < set_size; i++) {
                table0[i].has_value = false;
                table1[i].has_value = false;
            }

            // Copy over the old entries, but only the ones that had values
            for(int i = 0; i < size_old; i++) {
                if (table0_old[i].has_value) {
                    add(table0_old[i].value);
                }

                if (table1_old[i].has_value) {
                    add(table1_old[i].value);
                }
            }

            // Delete old tables
            delete[] table0_old;
            delete[] table1_old;
        }

        // Swap a new entry, return the old one
        entry swap(entry* table, T value, int index) {
            entry entry_old = table[index];

            table[index].value = value;
            table[index].has_value = true;

            return entry_old;
        }

    public:

        concurrent_set(int size, int num_locks, int limit) {
            this->set_size = size;
            this->offset = offset;
            this->limit = limit;

            table0 = new entry[set_size];
            table1 = new entry[set_size];

            for(int i = 0; i < set_size; i++) {
                table0[i].has_value = false;
                table1[i].has_value = false;
            }

            std::vector<std::mutex> locks0(num_locks);
            std::vector<std::mutex> locks1(num_locks);

            lock_table0.swap(locks0);
            lock_table1.swap(locks1);
        }

        ~concurrent_set(){
            delete[] table0;
            delete[] table1;
        }
        
        bool add(T value) {
            // If the table already contains the value return false
            if (contains(value)) {
                return false;
            }
            
            for(int i = 0; i < limit; i++) {
                entry swapped = swap(table0, value, hash0(value));
                if (!swapped.has_value) {
                    return true;
                }
                value = swapped.value;

                // Take the value we swapped from table0 and repeat the process for table1
                swapped = swap(table1, value, hash1(value));
                if (!swapped.has_value) {
                    return true;
                }
                value = swapped.value;
            }

            // We've gone <limit> iterations, the table is probably full, resize it
            resize();
            add(value);

            return true;
        }

        bool remove(T value){
            // Check if the value is in table0, if so, remove it
            int index = hash0(value);

            if (table0[index].has_value && table0[index].value == value) {
                table0[index].has_value = false;
                return true;
            }

            // Check if the value is in table1, if so, remove it
            index = hash1(value);

            if (table1[index].has_value && table1[index].value == value) {
                table1[index].has_value = false;
                return true;
            }

            // The value wasn't in either table, return false
            return false;
        }

        bool contains(T value){
            // Check if the value is in table0
            int index = hash0(value);

            if (table0[index].has_value && table0[index].value == value) {
                return true;
            }

            // Check if the value is in table1
            index = hash1(value);

            if (table1[index].has_value && table1[index].value == value) {
                return true;
            }

            // The value wasn't in either table, return false
            return false;
        }

        int size() {
            int count = 0;
            
            // Iterate over both tables, add to count whenever we hit an element
            for(int i = 0; i < set_size; i++) {
                if (table0[i].has_value) {
                    count++;
                }

                if (table1[i].has_value) {
                    count++;
                }
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