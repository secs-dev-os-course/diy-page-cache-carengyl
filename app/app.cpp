#include <iostream>
#include <unordered_map>
#include <list>
#include <vector>
#include <Windows.h>
#include <mutex>
#include <fstream>
#include <cstring>

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
        auto& block = it.second;

        // Если блок грязный, записываем его на диск
        if (block.dirty) {
            DWORD written;
            SetFilePointerEx(block.file_handle, block.offset, nullptr, FILE_BEGIN);
            if (!WriteFile(block.file_handle, block.data.data(), BLOCK_SIZE, &written, nullptr)) {
                std::cerr << "Error evicting dirty block to disk: " << GetLastError() << std::endl;
            } else {
                std::cout << "Evicted dirty block to disk: " << it.first << std::endl;
            }
        }

        // Удаляем блок из кеша
        cache_map.erase(it.first);
        cache_list.pop_back();
    }

public:
    BlockCache() : max_blocks(CACHE_SIZE / BLOCK_SIZE) {}

    CacheBlock* get_block(HANDLE file_handle, LARGE_INTEGER offset) {
        std::lock_guard<std::mutex> lock(cache_mutex);
        LONGLONG key = offset.QuadPart;

        // Если блок уже в кеше, перемещаем его в начало списка (LRU)
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
        block.data.resize(BLOCK_SIZE, 0);
        block.dirty = false;
        block.file_handle = file_handle;

        DWORD read;
        SetFilePointerEx(file_handle, offset, nullptr, FILE_BEGIN);
        if (!ReadFile(file_handle, block.data.data(), BLOCK_SIZE, &read, nullptr)) {
            std::cerr << "Error reading block from disk: " << GetLastError() << std::endl;
        } else {
            std::cout << "Loaded block from disk: " << key << std::endl;
        }

        cache_list.emplace_front(key, block);
        cache_map[key] = cache_list.begin();

        return &cache_list.begin()->second;
    }

    void mark_dirty(HANDLE file_handle, LARGE_INTEGER offset) {
        std::lock_guard<std::mutex> lock(cache_mutex);
        LONGLONG key = offset.QuadPart;
        if (cache_map.find(key) != cache_map.end()) {
            cache_map[key]->second.dirty = true;
            std::cout << "Marking block as dirty: " << key << std::endl;
        }
    }

    void sync(HANDLE file_handle) {
        std::lock_guard<std::mutex> lock(cache_mutex);
        for (auto it = cache_list.begin(); it != cache_list.end(); ++it) {
            auto& block = it->second;
            if (block.file_handle == file_handle && block.dirty) {
                DWORD written;
                SetFilePointerEx(block.file_handle, block.offset, nullptr, FILE_BEGIN);
                if (!WriteFile(block.file_handle, block.data.data(), BLOCK_SIZE, &written, nullptr)) {
                    std::cerr << "Error syncing dirty block to disk: " << GetLastError() << std::endl;
                } else {
                    std::cout << "Synced dirty block to disk: " << it->first << std::endl;
                    block.dirty = false; // Сбрасываем флаг "грязного" блока
                }
            }
        }
    }
};

BlockCache global_cache;

extern "C" {
    HANDLE lab2_open(const char* path) {
        HANDLE file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            std::cerr << "Error opening file: " << GetLastError() << std::endl;
        }
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
            offset.QuadPart = SetFilePointerEx(file, {0}, &offset, FILE_CURRENT); // Текущий указатель в файле
            LARGE_INTEGER block_offset;
            block_offset.QuadPart = (offset.QuadPart / BLOCK_SIZE) * BLOCK_SIZE; // Смещение на начало блока

            CacheBlock* block = global_cache.get_block(file, block_offset);
            size_t block_start = offset.QuadPart % BLOCK_SIZE;  // Смещение внутри блока
            size_t to_read = std::min(count, BLOCK_SIZE - block_start); // Сколько можно прочитать из блока

            // Копируем данные из блока в буфер
            memcpy(buf, block->data.data() + block_start, to_read);

            buf = (char*)buf + to_read;  // Сдвигаем указатель на считанные данные
            count -= to_read;  // Уменьшаем количество оставшихся данных для чтения
            bytes_read += to_read;  // Увеличиваем счетчик прочитанных байт

            offset.QuadPart += to_read;
            SetFilePointerEx(file, offset, nullptr, FILE_BEGIN);  // Перемещаем указатель в файл
        }
        return bytes_read;
    }

    ssize_t lab2_write(HANDLE file, const void* buf, size_t count) {
        size_t bytes_written = 0;
        while (count > 0) {
            LARGE_INTEGER offset;
            SetFilePointerEx(file, {0}, &offset, FILE_CURRENT);  // Текущий указатель в файле
            LARGE_INTEGER block_offset;
            block_offset.QuadPart = (offset.QuadPart / BLOCK_SIZE) * BLOCK_SIZE; // Смещение на начало блока

            CacheBlock* block = global_cache.get_block(file, block_offset);
            size_t block_start = offset.QuadPart % BLOCK_SIZE;  // Смещение внутри блока
            size_t to_write = std::min(count, BLOCK_SIZE - block_start);  // Сколько можно записать в блок

            // Записываем данные в кеш
            memcpy(block->data.data() + block_start, buf, to_write);
            block->dirty = true; // Отметим блок как грязный

            // Если записываем в середину блока, обнуляем остаток блока
            if (block_start + to_write < BLOCK_SIZE) {
                std::fill(block->data.begin() + block_start + to_write, block->data.end(), 0);
            }

            buf = (const char*)buf + to_write;  // Сдвигаем указатель на записанные данные
            count -= to_write;  // Уменьшаем количество оставшихся данных для записи
            bytes_written += to_write;  // Увеличиваем счетчик записанных байт

            offset.QuadPart += to_write;
            SetFilePointerEx(file, offset, nullptr, FILE_BEGIN);  // Перемещаем указатель в файл
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
