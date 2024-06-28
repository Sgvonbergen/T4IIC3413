#include <fstream>
#include <iostream>
#include <vector>

#include "relational_model/system.h"
#include "third_party/cli11/CLI11.hpp"

struct WriteLine {
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

// Get a uint32_t from char pointer. make sure buffer is of at least size 4 bytes
uint32_t get_uint32(uint32_t start, char* buffer) {
    uint32_t res;
    char* res_bytes = reinterpret_cast<char*>(&res);

    res_bytes[0] = buffer[start + 0];
    res_bytes[1] = buffer[start + 1];
    res_bytes[2] = buffer[start + 2];
    res_bytes[3] = buffer[start + 3];

    return res;
}

// Read logfile into a vector of lines of variable length
void read_log(std::fstream& log_file, std::vector<char*> *out) {
    LogType log_type;
    char* log_type_buffer = new char[1];
    char* line_buffer;
    while (log_file.good()) {
        log_file.read(log_type_buffer, 1);
        log_type = static_cast<LogType>(*log_type_buffer);
        switch (log_type) {
            case LogType::END:
            case LogType::START:
            case LogType::ABORT:
            case LogType::COMMIT: {
                line_buffer = new char[5];
                line_buffer[0] = *log_type_buffer;
                log_file.read(line_buffer + 1, 4);
                break;
            }
            case LogType::END_CHKP: {
                line_buffer = new char[1];
                line_buffer[0] = *log_type_buffer;
                break;
            }
            case LogType::START_CHKP: {
                char* t_amount_buffer = new char[4];
                log_file.read(t_amount_buffer, 4);
                uint32_t transactions_amount = static_cast<uint32_t>(*t_amount_buffer);
                line_buffer = new char[5 + 4*transactions_amount];
                line_buffer[0] = *log_type_buffer;
                for (int i = 0; i < 4; i++) {
                    line_buffer[i + 1] = t_amount_buffer[i];
                }
                log_file.read(line_buffer + 5, 4*transactions_amount);
                delete[] t_amount_buffer;
                break;
            }
            
            case LogType::WRITE_U: {
                char* params_buffer = new char[16];
                log_file.read(params_buffer, 16);
                char* len_buffer = new char[4];
                log_file.read(len_buffer, 4);
                uint32_t data_len = static_cast<uint32_t>(*len_buffer);
                line_buffer = new char[21 + data_len];
                line_buffer[0] = *log_type_buffer;
                for (int i = 0; i < 16; i++) {
                    line_buffer[i + 1] = params_buffer[i];
                }
                for (int i = 0; i < 4; i++) {
                    line_buffer[i + 17] = len_buffer[i];
                }
                log_file.read(line_buffer + 21, data_len);
                delete[] params_buffer;
                delete[] len_buffer;
                break;
            }
            case LogType::WRITE_UR: {
                char* params_buffer = new char[16];
                log_file.read(params_buffer, 16);
                char* len_buffer = new char[4];
                log_file.read(len_buffer, 4);
                uint32_t data_len = static_cast<uint32_t>(*len_buffer);
                line_buffer = new char[21 + 2*data_len];
                line_buffer[0] = *log_type_buffer;
                for (int i = 0; i < 16; i++) {
                    line_buffer[i + 1] = params_buffer[i];
                }
                for (int i = 0; i < 4; i++) {
                    line_buffer[i + 17] = len_buffer[i];
                }
                log_file.read(line_buffer + 21, 2*data_len);
                delete[] params_buffer;
                delete[] len_buffer;
                break;
            }
        }
        out->push_back(line_buffer);
    }
    delete[] log_type_buffer;
}

void get_undo_redo_transactions(std::vector<char*> *lines, std::set<uint32_t> *undo, std::set<uint32_t> *redo) {
    LogType log_type;
    for (uint32_t i = 0; i < lines->size(); i++) {
        char* line = lines->at(i);
        log_type = static_cast<LogType>(line[0]);
        switch (log_type) {
            case LogType::START: {
                uint32_t transaction_id = get_uint32(1, line);
                undo->insert(transaction_id);
                break;
            }
            case LogType::END: {
                uint32_t transaction_id = get_uint32(1, line);
                redo->erase(transaction_id);
                break;
            }
            case LogType::COMMIT: {
                uint32_t transaction_id = get_uint32(1, line);
                redo->insert(transaction_id);
                undo->erase(transaction_id);
                break;
            }
            default: {
                break;
            }
        }
    }
}

void undo_transaction(std::vector<char*> *lines, uint32_t transaction_id) {
    LogType log_type;
    WriteLine write_line;
    for (int i = lines->size() - 1; i >= 0; i--) {
        char* line = lines->at(i);
        log_type = static_cast<LogType>(line[0]);
        switch (log_type) {
            case LogType::WRITE_UR: {
                write_line.log_type = log_type;
                write_line.transaction_id = get_uint32(1, line);
                if (write_line.transaction_id != transaction_id) {
                    break;
                }
                write_line.table_id = get_uint32(5, line);
                write_line.page_num = get_uint32(9, line);
                write_line.offset = get_uint32(13, line);
                write_line.len = get_uint32(17, line);
                FileId file_id = catalog.get_file_id(write_line.table_id);
                Page& page = buffer_mgr.get_page(file_id, write_line.page_num);
                char* start = page.data() + write_line.offset;
                char* data = line + 21;
                for (uint32_t j = 0; j < write_line.len; j++) {
                    start[j] = data[j];
                }
                page.make_dirty();
                page.unpin();
                break;
            }
            default: {
                break;
            }
        }
    }
}

void redo_transaction(std::vector<char*> *lines, uint32_t transaction_id) {
    LogType log_type;
    WriteLine write_line;
    for (uint32_t i = 0; i < lines->size(); i++) {
        char* line = lines->at(i);
        log_type = static_cast<LogType>(line[0]);
        switch (log_type) {
            case LogType::WRITE_UR: {
                write_line.log_type = log_type;
                write_line.transaction_id = get_uint32(1, line);
                if (write_line.transaction_id != transaction_id) {
                    break;
                }
                write_line.table_id = get_uint32(5, line);
                write_line.page_num = get_uint32(9, line);
                write_line.offset = get_uint32(13, line);
                write_line.len = get_uint32(17, line);
                FileId file_id = catalog.get_file_id(write_line.table_id);
                Page& page = buffer_mgr.get_page(file_id, write_line.page_num);
                char* start = page.data() + write_line.offset;
                char* data = line + 21 + write_line.len;
                for (uint32_t j = 0; j < write_line.len; j++) {
                    start[j] = data[j];
                }
                page.make_dirty();
                page.unpin();
                break;
            }
            default: {
                break;
            }
        }
    }
}

void execute_undo_redo(std::vector<char*> *lines, std::set<uint32_t> *undo, std::set<uint32_t> *redo) {
    for (std::set<uint32_t>::iterator it = undo->begin(); it != undo->end(); ++it) {
        undo_transaction(lines, *it);
    }
    for (std::set<uint32_t>::iterator it = redo->begin(); it != redo->end(); ++it) {
        redo_transaction(lines, *it);
    }
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

    // UndoRedo Method
    std::vector<char*>* lines = new std::vector<char*>();
    read_log(log_file, lines);
    log_file.close();
    std::set<uint32_t>* undo_transactions = new std::set<uint32_t>();
    std::set<uint32_t>* redo_transactions = new std::set<uint32_t>();
    get_undo_redo_transactions(lines, undo_transactions, redo_transactions);
    System system = System::init(db_directory, BufferManager::DEFAULT_BUFFER_SIZE);
    execute_undo_redo(lines, undo_transactions, redo_transactions);
    buffer_mgr.flush();
    return EXIT_SUCCESS;
}
