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

    // READING LOGFILE
    // Array of transaction ids to NOT undo. FREE LATER (1)
    uint32_t* commited_ids = new uint32_t[Page::SIZE];
    uint32_t commited_size = 0;

    uint32_t* transaction_ids = new uint32_t[Page::SIZE];
    uint32_t transaction_ids_size = 0;

    // Array of UndoLines. FREE LATER (2)
    UndoLine** undo_lines = new UndoLine*[Page::SIZE];
    char** undo_lines_data = new char*[Page::SIZE];
    uint32_t undo_lines_size = 0;
    LogType log_type;
    // FREE LATER (3)
    char* log_type_buffer = new char[1];
    while (log_file.good()) {
        log_file.read(log_type_buffer, 1);
        log_type = static_cast<LogType>(*log_type_buffer);
        if(log_type == LogType::WRITE_U) {
            // New UndoLine. FREE LATER (4)
            UndoLine* undo_line;
            undo_line = new UndoLine();
            undo_line->log_type = log_type;
            undo_line->transaction_id = read_uint32(log_file);
            undo_line->table_id = read_uint32(log_file);
            undo_line->page_num = read_uint32(log_file);
            undo_line->offset = read_uint32(log_file);
            undo_line->len = read_uint32(log_file);

            undo_lines[undo_lines_size] = undo_line;
            // New Data. FREE LATER (5)
            char* data_buffer = new char[undo_line->len];
            log_file.read(data_buffer, undo_line->len);
            undo_lines_data[undo_lines_size] = data_buffer;

            undo_lines_size++;
        }
        if (log_type == LogType::ABORT || log_type == LogType::COMMIT) {
            commited_ids[commited_size] = read_uint32(log_file);
            commited_size++;
        }
        if (log_type == LogType::START) {
            transaction_ids[transaction_ids_size] = read_uint32(log_file);
            transaction_ids_size++;
        }
    }
    delete[] log_type_buffer;// (3)
    log_file.close();

    // RECOVERY
    System system = System::init(db_directory, BufferManager::DEFAULT_BUFFER_SIZE);
    uint32_t* transactions_to_undo = new uint32_t[1 + transaction_ids_size - commited_size]; // (6)
    uint32_t transactions_to_undo_size = 0;
    for (uint32_t i = 0; i < transaction_ids_size; i++) {
        bool undo = true;
        for (uint32_t j = 0; j < commited_size; j++) {
            if (transaction_ids[i] == commited_ids[j]) {
                undo = false;
                break;
            }
        }
        if (undo) {
            transactions_to_undo[transactions_to_undo_size] = transaction_ids[i];
            transactions_to_undo_size++;
        }
    }
    delete[] transaction_ids;
    delete[] commited_ids; // (1)
    // UndoLine* line2 = undo_lines[0];
    // std::cout << "First Write: " << line2->transaction_id << " on table " << line2->table_id << " page " << line2->page_num << " offset " << line2->offset << " len " << line2->len << std::endl;
    for (int i = undo_lines_size - 1; i >= 0; i--) {
        UndoLine* line = undo_lines[i];
        bool undo = false;
        for (uint32_t j = 0; j < transactions_to_undo_size; j++) {
            if (line->transaction_id == transactions_to_undo[j]) {
                undo = true;
                break;
            }
        }
        if (undo) {
            std::cout << i << ": ";
            std::cout << "Undoing Write of transaction: " << line->transaction_id << " on table " << line->table_id << " page " << line->page_num << " offset " << line->offset << " len " << line->len << std::endl;
            FileId file_id = catalog.get_file_id(line->table_id);
            Page& page = buffer_mgr.get_page(file_id, line->page_num);
            page.make_dirty();
            // Write old data
            char* start = page.data() + line->offset;
            char* data = undo_lines_data[i];
            for (uint32_t j = 0; j < line->len; j++) {
                start[j] = data[j];
            }
            // std::cout << "Data Overwritten Successfully" << std::endl;
            page.unpin();
        }
    }
    std::cout << "Recovery done" << std::endl;
    // Free allocated memory
    delete[] transactions_to_undo; // (6)
    for (uint32_t i = 0; i < undo_lines_size; i++) {
        delete[] undo_lines[i]; // (4)
        delete[] undo_lines_data[i]; // (5)
    }
    delete[] undo_lines; // (2)
    delete[] undo_lines_data;
    buffer_mgr.flush();
    std::cout << "Memory Released Successfully" << std::endl;
}
