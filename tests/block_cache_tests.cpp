#include <gtest/gtest.h>
#include <fstream>
#include "../app/app.cpp"

// Создаем временный файл для тестов
class BlockCacheTest : public ::testing::Test {
protected:
    std::string test_file = "test_file.bin";

    void SetUp() override {
        std::ofstream file(test_file, std::ios::binary | std::ios::out);
        std::vector<char> data(64 * 1024, 'A'); // Заполняем файл 64 КБ данных
        file.write(data.data(), data.size());
        file.close();
    }

    void TearDown() override {
        std::remove(test_file.c_str());
    }
};

// Тест открытия и закрытия файла
TEST_F(BlockCacheTest, OpenAndCloseFile) {
    HANDLE file = lab2_open(test_file.c_str());
    ASSERT_NE(file, INVALID_HANDLE_VALUE);
    EXPECT_EQ(lab2_close(file), 0);
}

// Тест чтения данных
TEST_F(BlockCacheTest, ReadData) {
    HANDLE file = lab2_open(test_file.c_str());
    ASSERT_NE(file, INVALID_HANDLE_VALUE);

    char buffer[BLOCK_SIZE] = {0};
    ssize_t bytes_read = lab2_read(file, buffer, BLOCK_SIZE);

    EXPECT_EQ(bytes_read, BLOCK_SIZE);
    EXPECT_EQ(buffer[0], 'A'); // Проверяем, что данные корректны

    EXPECT_EQ(lab2_close(file), 0);
}

// Тест записи и чтения данных
TEST_F(BlockCacheTest, WriteAndReadData) {
    HANDLE file = lab2_open(test_file.c_str());
    ASSERT_NE(file, INVALID_HANDLE_VALUE);

    const char data[] = "Hello, World!";
    size_t data_size = strlen(data);
    LARGE_INTEGER offset = {};
    lab2_lseek(file, offset, FILE_BEGIN);

    // Записываем данные
    ssize_t bytes_written = lab2_write(file, data, data_size);
    EXPECT_EQ(bytes_written, data_size);

    // Синхронизируем данные
    EXPECT_EQ(lab2_fsync(file), 0);

    // Перемещаем указатель на начало и читаем данные
    lab2_lseek(file, offset, FILE_BEGIN);
    char buffer[data_size] = {0};
    ssize_t bytes_read = lab2_read(file, buffer, data_size);

    EXPECT_EQ(bytes_read, data_size);
    EXPECT_EQ(memcmp(buffer, data, data_size), 0); // Проверяем корректность данных

    EXPECT_EQ(lab2_close(file), 0);
}

// Тест записи на границе блока
TEST_F(BlockCacheTest, WriteAtBlockBoundary) {
    HANDLE file = lab2_open(test_file.c_str());
    ASSERT_NE(file, INVALID_HANDLE_VALUE);

    const char data[] = "BlockBoundaryTest";
    size_t data_size = strlen(data);

    // Устанавливаем указатель на границу блока
    LARGE_INTEGER offset;
    offset.QuadPart = BLOCK_SIZE - data_size / 2;
    lab2_lseek(file, offset, FILE_BEGIN);

    // Записываем данные
    ssize_t bytes_written = lab2_write(file, data, data_size);
    EXPECT_EQ(bytes_written, data_size);

    // Синхронизируем данные
    EXPECT_EQ(lab2_fsync(file), 0);

    // Перемещаем указатель и читаем данные
    lab2_lseek(file, offset, FILE_BEGIN);
    char buffer[data_size] = {0};
    ssize_t bytes_read = lab2_read(file, buffer, data_size);

    EXPECT_EQ(bytes_read, data_size);
    EXPECT_EQ(memcmp(buffer, data, data_size), 0); // Проверяем корректность данных

    EXPECT_EQ(lab2_close(file), 0);
}

// Тест вытеснения блоков из кэша
TEST_F(BlockCacheTest, CacheEviction) {
    HANDLE file = lab2_open(test_file.c_str());
    ASSERT_NE(file, INVALID_HANDLE_VALUE);

    const int num_blocks = CACHE_SIZE / BLOCK_SIZE + 1; // Читаем больше блоков, чем вмещает кэш
    for (int i = 0; i < num_blocks; ++i) {
        LARGE_INTEGER offset;
        offset.QuadPart = static_cast<LONGLONG>(i) * BLOCK_SIZE;
        lab2_lseek(file, offset, FILE_BEGIN);

        char buffer[BLOCK_SIZE];
        lab2_read(file, buffer, BLOCK_SIZE);
    }

    // Проверяем, что первый блок был вытеснен и загружен с диска
    LARGE_INTEGER offset = {};
    lab2_lseek(file, offset, FILE_BEGIN);
    char buffer[BLOCK_SIZE] = {0};
    ssize_t bytes_read = lab2_read(file, buffer, BLOCK_SIZE);

    EXPECT_EQ(bytes_read, BLOCK_SIZE);
    EXPECT_EQ(buffer[0], 'A'); // Проверяем, что данные корректны

    EXPECT_EQ(lab2_close(file), 0);
}

// Тест синхронизации данных с диском
TEST_F(BlockCacheTest, SyncDataToDisk) {
    HANDLE file = lab2_open(test_file.c_str());
    ASSERT_NE(file, INVALID_HANDLE_VALUE);

    const char data[BLOCK_SIZE] = "Persistent data";
    LARGE_INTEGER offset = {};
    lab2_lseek(file, offset, FILE_BEGIN);

    // Записываем данные
    lab2_write(file, data, sizeof(data));

    // Синхронизируем данные
    EXPECT_EQ(lab2_fsync(file), 0);

    // Закрываем и открываем файл заново
    EXPECT_EQ(lab2_close(file), 0);
    file = lab2_open(test_file.c_str());
    ASSERT_NE(file, INVALID_HANDLE_VALUE);

    // Читаем данные
    lab2_lseek(file, offset, FILE_BEGIN);
    char buffer[BLOCK_SIZE] = {0};
    ssize_t bytes_read = lab2_read(file, buffer, sizeof(data));

    EXPECT_EQ(bytes_read, sizeof(data));
    EXPECT_EQ(memcmp(buffer, data, sizeof(data)), 0); // Проверяем корректность данных

    EXPECT_EQ(lab2_close(file), 0);
}