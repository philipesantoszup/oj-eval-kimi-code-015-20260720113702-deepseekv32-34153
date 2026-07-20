#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <filesystem>

using namespace std;

const string DATA_FILE = "storage.dat";
const int MAX_KEY_LEN = 64;
const int HASH_SIZE = 100003; // Prime number for better distribution
const int INVALID_INDEX = -1;

struct Entry {
    char key[MAX_KEY_LEN + 1];
    int value;
    int next; // next entry in chain, -1 for end
    bool valid;
    
    Entry() : next(INVALID_INDEX), valid(false) {
        memset(key, 0, sizeof(key));
    }
    
    bool matches(const string& k, int v) const {
        return valid && strcmp(key, k.c_str()) == 0 && value == v;
    }
    
    bool matchesKey(const string& k) const {
        return valid && strcmp(key, k.c_str()) == 0;
    }
};

class DiskHashTable {
private:
    fstream file;
    vector<int> hash_table; // in memory
    int free_head; // head of free list
    int entry_count;
    bool dirty; // whether hash table needs to be written back
    
    // FNV-1a hash function
    size_t hashKey(const string& key) const {
        size_t hash = 14695981039346656037ULL;
        for (char c : key) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 1099511628211ULL;
        }
        return hash % HASH_SIZE;
    }
    
    int getHashTableOffset() const {
        return sizeof(free_head) + sizeof(entry_count);
    }
    
    int getHashTableSize() const {
        return HASH_SIZE * sizeof(int);
    }
    
    int getEntriesOffset() const {
        return getHashTableOffset() + getHashTableSize();
    }
    
    void readHashTable() {
        file.seekg(getHashTableOffset());
        file.read(reinterpret_cast<char*>(hash_table.data()), getHashTableSize());
    }
    
    void writeHashTable() {
        file.seekp(getHashTableOffset());
        file.write(reinterpret_cast<const char*>(hash_table.data()), getHashTableSize());
        file.flush();
        dirty = false;
    }
    
    int allocateEntry() {
        if (free_head != INVALID_INDEX) {
            int index = free_head;
            // Read next free pointer from the free entry
            int next_free;
            file.seekg(getEntriesOffset() + index * sizeof(Entry) + offsetof(Entry, next));
            file.read(reinterpret_cast<char*>(&next_free), sizeof(next_free));
            free_head = next_free;
            return index;
        } else {
            // Append new entry
            int index = entry_count;
            entry_count++;
            return index;
        }
    }
    
    void freeEntry(int index) {
        // Write free_head to entry.next
        file.seekp(getEntriesOffset() + index * sizeof(Entry) + offsetof(Entry, next));
        file.write(reinterpret_cast<const char*>(&free_head), sizeof(free_head));
        
        // Mark as invalid
        char valid_flag = 0;
        file.seekp(getEntriesOffset() + index * sizeof(Entry) + offsetof(Entry, valid));
        file.write(&valid_flag, sizeof(valid_flag));
        
        file.flush();
        free_head = index;
    }
    
    void readEntry(int index, Entry& entry) {
        file.seekg(getEntriesOffset() + index * sizeof(Entry));
        file.read(reinterpret_cast<char*>(&entry), sizeof(entry));
    }
    
    void writeEntry(int index, const Entry& entry) {
        file.seekp(getEntriesOffset() + index * sizeof(Entry));
        file.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
        file.flush();
    }
    
public:
    DiskHashTable() : free_head(INVALID_INDEX), entry_count(0), dirty(false) {
        hash_table.resize(HASH_SIZE, INVALID_INDEX);
        
        if (filesystem::exists(DATA_FILE)) {
            file.open(DATA_FILE, ios::binary | ios::in | ios::out);
            // Read free_head and entry_count from beginning of file
            file.seekg(0);
            file.read(reinterpret_cast<char*>(&free_head), sizeof(free_head));
            file.read(reinterpret_cast<char*>(&entry_count), sizeof(entry_count));
            
            // Read hash table from file
            readHashTable();
        } else {
            file.open(DATA_FILE, ios::binary | ios::in | ios::out | ios::trunc);
            free_head = INVALID_INDEX;
            entry_count = 0;
            
            // Write initial free_head and entry_count
            file.seekp(0);
            file.write(reinterpret_cast<const char*>(&free_head), sizeof(free_head));
            file.write(reinterpret_cast<const char*>(&entry_count), sizeof(entry_count));
            
            // Initialize hash table with -1
            writeHashTable();
        }
    }
    
    ~DiskHashTable() {
        if (file.is_open()) {
            // Update free_head and entry_count at beginning of file
            file.seekp(0);
            file.write(reinterpret_cast<const char*>(&free_head), sizeof(free_head));
            file.write(reinterpret_cast<const char*>(&entry_count), sizeof(entry_count));
            
            // Write hash table if dirty
            if (dirty) {
                writeHashTable();
            }
            
            file.close();
        }
    }
    
    void insert(const string& key, int value) {
        size_t h = hashKey(key);
        
        // Check if entry already exists
        int curr = hash_table[h];
        Entry entry;
        while (curr != INVALID_INDEX) {
            readEntry(curr, entry);
            if (entry.matches(key, value)) {
                return; // Already exists
            }
            curr = entry.next;
        }
        
        // Create new entry
        int index = allocateEntry();
        Entry new_entry;
        strncpy(new_entry.key, key.c_str(), MAX_KEY_LEN);
        new_entry.key[MAX_KEY_LEN] = '\0';
        new_entry.value = value;
        new_entry.next = hash_table[h];
        new_entry.valid = true;
        
        writeEntry(index, new_entry);
        
        // Update hash table in memory
        hash_table[h] = index;
        dirty = true;
    }
    
    void remove(const string& key, int value) {
        size_t h = hashKey(key);
        int curr = hash_table[h];
        int prev = INVALID_INDEX;
        Entry entry;
        
        while (curr != INVALID_INDEX) {
            readEntry(curr, entry);
            
            if (entry.matches(key, value)) {
                if (prev == INVALID_INDEX) {
                    // Removing head of chain
                    hash_table[h] = entry.next;
                } else {
                    // Update previous entry's next pointer
                    file.seekp(getEntriesOffset() + prev * sizeof(Entry) + offsetof(Entry, next));
                    file.write(reinterpret_cast<const char*>(&entry.next), sizeof(entry.next));
                    file.flush();
                }
                
                // Free the entry
                freeEntry(curr);
                
                dirty = true;
                return;
            }
            
            prev = curr;
            curr = entry.next;
        }
    }
    
    vector<int> find(const string& key) {
        vector<int> result;
        size_t h = hashKey(key);
        int curr = hash_table[h];
        Entry entry;
        
        while (curr != INVALID_INDEX) {
            readEntry(curr, entry);
            if (entry.matchesKey(key)) {
                result.push_back(entry.value);
            }
            curr = entry.next;
        }
        
        sort(result.begin(), result.end());
        return result;
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    
    int n;
    cin >> n;
    cin.ignore();
    
    DiskHashTable db;
    
    for (int i = 0; i < n; i++) {
        string line;
        getline(cin, line);
        
        size_t space1 = line.find(' ');
        if (space1 == string::npos) {
            continue; // Invalid command
        }
        
        string cmd = line.substr(0, space1);
        
        if (cmd == "insert") {
            size_t space2 = line.find(' ', space1 + 1);
            if (space2 == string::npos) {
                continue; // Invalid command
            }
            
            string key = line.substr(space1 + 1, space2 - space1 - 1);
            int value = stoi(line.substr(space2 + 1));
            
            db.insert(key, value);
        }
        else if (cmd == "delete") {
            size_t space2 = line.find(' ', space1 + 1);
            if (space2 == string::npos) {
                continue; // Invalid command
            }
            
            string key = line.substr(space1 + 1, space2 - space1 - 1);
            int value = stoi(line.substr(space2 + 1));
            
            db.remove(key, value);
        }
        else if (cmd == "find") {
            string key = line.substr(space1 + 1);
            
            vector<int> values = db.find(key);
            
            if (values.empty()) {
                cout << "null\n";
            } else {
                for (size_t j = 0; j < values.size(); j++) {
                    if (j > 0) cout << " ";
                    cout << values[j];
                }
                cout << "\n";
            }
        }
    }
    
    return 0;
}