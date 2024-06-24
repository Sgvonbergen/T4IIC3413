#include <fstream>
#include <iostream>

#include "relational_model/system.h"
#include "third_party/cli11/CLI11.hpp"

struct UndoLine {
    LogType log_type;
    uint32_t transaction_id;
    uint32_t table_id;
    uint32_t page_num;
    uint32_t offset;
    uint32_t len;
};

uint32_t read_uint32(std::fstream& log_file) {
    uint32_t res;

    char small_buffer[4];
    log_file.read(small_buffer, 4);

    char* res_bytes = reinterpret_cast<char*>(&res);

    res_bytes[0] = small_buffer[0];
    res_bytes[1] = small_buffer[1];
    res_bytes[2] = small_buffer[2];
    res_bytes[3] = small_buffer[3];

    return res;
}


int main(int argc, char* argv[]) {
    std::string log_path;

    std::string db_directory;

    CLI::App app{"IIC 3413 DB"};
    app.get_formatter()->column_width(35);
    app.option_defaults()->always_capture_default();

    app.add_option("database", db_directory)
        ->description("Database directory")
        ->type_name("<path>")
        ->check(CLI::ExistingDirectory.description(""))
        ->required();

    app.add_option("log file", log_path)
        ->description("Log file")
        ->type_name("<path>")
        ->check(CLI::ExistingFile.description(""))
        ->required();

    CLI11_PARSE(app, argc, argv);

    std::fstream log_file(log_path, std::ios::binary|std::ios::in);

    if (log_file.fail()) {
        std::cerr << "Could not open the log at path: " << log_path << "\n";
        return EXIT_FAILURE;
    }
    auto system = System::init(db_directory, BufferManager::DEFAULT_BUFFER_SIZE);
    UndoLine* undo_lines = new UndoLine[Page::SIZE];
    uint32_t undo_lines_size = 0;
    while (log_file.good()) {
        char* log_type_buffer = new char[1];
        log_file.read(log_type_buffer, 1);
        LogType log_type = static_cast<LogType>(*log_type_buffer);
        if(log_type == LogType::WRITE_U) {
            UndoLine* undo_line;
            undo_line = new UndoLine();
            undo_line->log_type = log_type;
            undo_line->transaction_id = read_uint32(log_file);
            undo_line->table_id = read_uint32(log_file);
            undo_line->page_num = read_uint32(log_file);
            undo_line->offset = read_uint32(log_file);
            undo_line->len = read_uint32(log_file);

            undo_lines[undo_lines_size] = *undo_line;
            undo_lines_size++;

            // char* previous_data = new char[undo_line.len];
            // log_file.read(previous_data, undo_line.len);
        }
        delete[] log_type_buffer;
    }

    log_file.close();
}
