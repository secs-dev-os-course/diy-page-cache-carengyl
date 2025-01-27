#include <iostream>
#include <unordered_map>
#include <list>
#include <vector>
#include <Windows.h>
#include <mutex>

// Константы
constexpr size_t BLOCK_SIZE = 4096; // Размер блока
constexpr size_t CACHE_SIZE = 1024 * 1024 * 16; // Размер кеша 16 МБ

// Структура блока кеша
struct CacheBlock {
    LARGE_INTEGER offset;  // Смещение в файле
    std::vector<char> data; // Данные блока
    bool dirty; // Признак "грязного" блока
    HANDLE file_handle; // Дескриптор файла
};

class BlockCache {
private:
    size_t max_blocks;
    std::unordered_map<LONGLONG, std::list<std::pair<LONGLONG, CacheBlock>>::iterator> cache_map;
    std::list<std::pair<LONGLONG, CacheBlock>> cache_list;
    std::mutex cache_mutex;

    void evict_block() {
        if (cache_list.empty()) return;

        auto it = cache_list.back();
        if (it.second.dirty) {
            DWORD written;
            SetFilePointerEx(it.second.file_handle, it.second.offset, nullptr, FILE_BEGIN);
            WriteFile(it.second.file_handle, it.second.data.data(), BLOCK_SIZE, &written, nullptr);
        }
        cache_map.erase(it.first);
        cache_list.pop_back();
    }

public:
    BlockCache() : max_blocks(CACHE_SIZE / BLOCK_SIZE) {}

    CacheBlock* get_block(HANDLE file_handle, LARGE_INTEGER offset) {
        std::lock_guard<std::mutex> lock(cache_mutex);
        LONGLONG key = offset.QuadPart;

        // Если блок в кеше, перемещаем его в начало списка (LRU)
        if (cache_map.find(key) != cache_map.end()) {
            cache_list.splice(cache_list.begin(), cache_list, cache_map[key]);
            return &cache_map[key]->second;
        }

        // Если блока нет, загружаем его с диска
        if (cache_list.size() >= max_blocks) {
            evict_block();
        }

        CacheBlock block;
        block.offset = offset;
        block.data.resize(BLOCK_SIZE);
        block.dirty = false;
        block.file_handle = file_handle;

        DWORD read;
        SetFilePointerEx(file_handle, offset, nullptr, FILE_BEGIN);
        ReadFile(file_handle, block.data.data(), BLOCK_SIZE, &read, nullptr);

        cache_list.emplace_front(key, block);
        cache_map[key] = cache_list.begin();

        return &cache_list.begin()->second;
    }

    void mark_dirty(HANDLE file_handle, LARGE_INTEGER offset) {
        std::lock_guard<std::mutex> lock(cache_mutex);
        LONGLONG key = offset.QuadPart;
        if (cache_map.find(key) != cache_map.end()) {
            cache_map[key]->second.dirty = true;
        }
    }

    void sync(HANDLE file_handle) {
        std::lock_guard<std::mutex> lock(cache_mutex);
        for (auto it = cache_list.begin(); it != cache_list.end();) {
            if (it->second.file_handle == file_handle && it->second.dirty) {
                DWORD written;
                SetFilePointerEx(it->second.file_handle, it->second.offset, nullptr, FILE_BEGIN);
                WriteFile(it->second.file_handle, it->second.data.data(), BLOCK_SIZE, &written, nullptr);
                it->second.dirty = false;
            }
            ++it;
        }
    }
};

BlockCache global_cache;

extern "C" {
    HANDLE lab2_open(const char* path) {
        HANDLE file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        return file;
    }

    int lab2_close(HANDLE file) {
        global_cache.sync(file);
        return CloseHandle(file) ? 0 : -1;
    }

    ssize_t lab2_read(HANDLE file, void* buf, size_t count) {
        size_t bytes_read = 0;
        while (count > 0) {
            LARGE_INTEGER offset;
            offset.QuadPart = SetFilePointerEx(file, {0}, &offset, FILE_CURRENT);
            LARGE_INTEGER block_offset = { offset.QuadPart / BLOCK_SIZE * BLOCK_SIZE };

            CacheBlock* block = global_cache.get_block(file, block_offset);
            size_t block_start = offset.QuadPart % BLOCK_SIZE;
            size_t to_read = std::min(count, BLOCK_SIZE - block_start);

            memcpy(buf, block->data.data() + block_start, to_read);

            buf = (char*)buf + to_read;
            count -= to_read;
            bytes_read += to_read;

            offset.QuadPart += to_read;
            SetFilePointerEx(file, offset, nullptr, FILE_BEGIN);
        }
        return bytes_read;
    }

    ssize_t lab2_write(HANDLE file, const void* buf, size_t count) {
        size_t bytes_written = 0;
        while (count > 0) {
            LARGE_INTEGER offset;
            offset.QuadPart = SetFilePointerEx(file, {0}, &offset, FILE_CURRENT);
            LARGE_INTEGER block_offset = { offset.QuadPart / BLOCK_SIZE * BLOCK_SIZE };

            CacheBlock* block = global_cache.get_block(file, block_offset);
            size_t block_start = offset.QuadPart % BLOCK_SIZE;
            size_t to_write = std::min(count, BLOCK_SIZE - block_start);

            memcpy(block->data.data() + block_start, buf, to_write);
            block->dirty = true;

            buf = (const char*)buf + to_write;
            count -= to_write;
            bytes_written += to_write;

            offset.QuadPart += to_write;
            SetFilePointerEx(file, offset, nullptr, FILE_BEGIN);
        }
        return bytes_written;
    }

    LARGE_INTEGER lab2_lseek(HANDLE file, LARGE_INTEGER offset, DWORD whence) {
        LARGE_INTEGER new_offset;
        SetFilePointerEx(file, offset, &new_offset, whence);
        return new_offset;
    }

    int lab2_fsync(HANDLE file) {
        global_cache.sync(file);
        return 0;
    }
}
