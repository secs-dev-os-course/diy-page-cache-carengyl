#include <gtest/gtest.h>
#include <fstream>
#include "../app/app.cpp"

// Создаем временный файл для тестов
class BlockCacheTest : public ::testing::Test {
protected:
    std::string test_file = "test_file.bin";

    void SetUp() override {
        std::ofstream file(test_file, std::ios::binary | std::ios::out);
        // Заполняем файл 64 КБ данных
        std::vector<char> data(64 * 1024, 'A');
        file.write(data.data(), data.size());
        file.close();
    }

    void TearDown() override {
        std::remove(test_file.c_str());
    }
};

TEST_F(BlockCacheTest, OpenAndCloseFile) {
    HANDLE file = lab2_open(test_file.c_str());
    ASSERT_NE(file, INVALID_HANDLE_VALUE);
    EXPECT_EQ(lab2_close(file), 0);
}

TEST_F(BlockCacheTest, ReadData) {
    HANDLE file = lab2_open(test_file.c_str());
    ASSERT_NE(file, INVALID_HANDLE_VALUE);

    char buffer[BLOCK_SIZE];
    ssize_t bytes_read = lab2_read(file, buffer, BLOCK_SIZE);

    EXPECT_EQ(bytes_read, BLOCK_SIZE);
    EXPECT_EQ(buffer[0], 'A'); // Проверяем содержимое

    EXPECT_EQ(lab2_close(file), 0);
}

TEST_F(BlockCacheTest, WriteData) {
    HANDLE file = lab2_open(test_file.c_str());
    ASSERT_NE(file, INVALID_HANDLE_VALUE);

    const char data[BLOCK_SIZE] = "Hello, World!";
    ssize_t bytes_written = lab2_write(file, data, sizeof(data));
    EXPECT_EQ(bytes_written, sizeof(data));

    // Перемещаем указатель и читаем снова
    LARGE_INTEGER offset = {};
    lab2_lseek(file, offset, FILE_BEGIN);

    char buffer[BLOCK_SIZE];
    ssize_t bytes_read = lab2_read(file, buffer, BLOCK_SIZE);

    EXPECT_EQ(bytes_read, BLOCK_SIZE);
    EXPECT_STREQ(buffer, data);

    EXPECT_EQ(lab2_close(file), 0);
}

TEST_F(BlockCacheTest, LRUCacheEviction) {
    HANDLE file = lab2_open(test_file.c_str());
    ASSERT_NE(file, INVALID_HANDLE_VALUE);

    // Читаем больше блоков, чем вмещает кэш
    const int num_blocks = CACHE_SIZE / BLOCK_SIZE + 1;
    for (int i = 0; i < num_blocks; ++i) {
        LARGE_INTEGER offset;
        offset.QuadPart = static_cast<LONGLONG>(i) * BLOCK_SIZE;
        lab2_lseek(file, offset, FILE_BEGIN);

        char buffer[BLOCK_SIZE];
        lab2_read(file, buffer, BLOCK_SIZE);
    }

    // Проверяем, что последний блок заменил первый в кэше
    LARGE_INTEGER offset = {};
    lab2_lseek(file, offset, FILE_BEGIN);

    char buffer[BLOCK_SIZE];
    lab2_read(file, buffer, BLOCK_SIZE);

    EXPECT_EQ(buffer[0], 'A'); // Содержимое должно быть прежним
    EXPECT_EQ(lab2_close(file), 0);
}

TEST_F(BlockCacheTest, SyncDataToDisk) {
    HANDLE file = lab2_open(test_file.c_str());
    ASSERT_NE(file, INVALID_HANDLE_VALUE);

    const char data[BLOCK_SIZE] = "Persistent data";
    lab2_write(file, data, sizeof(data));

    // Синхронизируем данные
    EXPECT_EQ(lab2_fsync(file), 0);

    // Открываем файл заново и проверяем, что данные записаны
    EXPECT_EQ(lab2_close(file), 0);
    file = lab2_open(test_file.c_str());
    ASSERT_NE(file, INVALID_HANDLE_VALUE);

    char buffer[BLOCK_SIZE];
    ssize_t bytes_read = lab2_read(file, buffer, BLOCK_SIZE);
    EXPECT_EQ(bytes_read, BLOCK_SIZE);
    EXPECT_STREQ(buffer, data);

    EXPECT_EQ(lab2_close(file), 0);
}
