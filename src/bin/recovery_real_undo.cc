#include <fstream>
#include <iostream>
#include <vector>

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

uint32_t read_uint32(std::ifstream& log_file) {
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

// Get transactions to undo and find the position to truncate the log
uint32_t get_undo_transactions_and_truncate_position(std::vector<char*> lines, std::set<uint32_t> out) {
    LogType log_type;
    bool found_start_checkpoint = false;
    bool found_end_checkpoint = false;
    uint32_t truncate_position = 0;
    std::set<uint32_t> active_transactions;
    std::set<uint32_t> finished_transactions;
    for (int i = lines.size() - 1; i >= 0; i--) {
        if (found_start_checkpoint && found_end_checkpoint) {
            truncate_position = i + 1;
            break;
        }
        char* line = lines[i];
        log_type = static_cast<LogType>(line[0]);
        switch (log_type) {
            case LogType::END_CHKP: {
                found_end_checkpoint = true;
                found_start_checkpoint = false;
                break;
            }
            case LogType::START_CHKP: {
                found_start_checkpoint = true;
                char* t_amount_buffer = new char[4];
                for (int j = 0; j < 4; j++) {
                    t_amount_buffer[j] = line[j + 1];
                }
                uint32_t transactions_amount = static_cast<uint32_t>(*t_amount_buffer);


                for (uint32_t j = 0; j < transactions_amount; j++) {
                    char* t_id_buffer = new char[4];
                    for (int k = 0; k < 4; k++) {
                        t_id_buffer[k] = line[5 + 4*j + k];
                    }
                    uint32_t transaction_id = static_cast<uint32_t>(*t_id_buffer);
                    if (found_end_checkpoint) {
                        finished_transactions.insert(transaction_id);
                    } else {
                        active_transactions.insert(transaction_id);
                    }
                    delete[] t_id_buffer;
                }
                break;
            }
            case LogType::ABORT:
            case LogType::COMMIT: {
                char* t_id_buffer = new char[4];
                for (int j = 0; j < 4; j++) {
                    t_id_buffer[j] = line[j + 1];
                }
                uint32_t transaction_id = static_cast<uint32_t>(*t_id_buffer);
                finished_transactions.insert(transaction_id);
                delete[] t_id_buffer;
                break;
            }
            default: {
                char* t_id_buffer = new char[4];
                for (int j = 0; j < 4; j++) {
                    t_id_buffer[j] = line[j + 1];
                }
                uint32_t transaction_id = static_cast<uint32_t>(*t_id_buffer);
                active_transactions.insert(transaction_id);
                delete[] t_id_buffer;
                break;
            }
        }
    }
    std::set_difference(active_transactions.begin(), active_transactions.end(),
                        finished_transactions.begin(), finished_transactions.end(),
                        std::inserter(out, out.begin()));
    return truncate_position;
}

int truncate_log(std::string log_path, uint32_t position, std::vector<char*> lines) {
    std::ofstream log_file(log_path, std::ios::binary|std::ios::out|std::ios::trunc);
    if (log_file.fail()) {
        std::cerr << "Could not open the log at path: " << log_path << "\n";
        return EXIT_FAILURE;
    }
    LogType log_type;
    char* line;
    for (int i = lines.size() - 1; i >= position; i--) {
        line = lines[i];
        log_type = static_cast<LogType>(lines[i][0]);
        switch (log_type) {
            case LogType::START:
            case LogType::ABORT:
            case LogType::COMMIT: {
                log_file.write(line, 5);
                break;
            }
            case LogType::END_CHKP: {
                log_file.write(line, 1);
                break;
            }
            case LogType::START_CHKP: {
                log_file.write(line, 5);
                uint32_t transactions_amount = get_uint32(1, line);
                log_file.write(line + 5, 4*transactions_amount);
                break;
            }
            case LogType::WRITE_U: {
                uint32_t len = get_uint32(17, line);
                log_file.write(line, 21 + len);
                break;
            }
        }
    }
    return EXIT_SUCCESS;
}

// Get pages and rewrite data found in logfile lines of specified transactions
void undo_after_position(std::vector<char*> lines, uint32_t end, std::set<uint32_t> undo_transactions) {
    LogType log_type;
    UndoLine undo_line;
    for (uint32_t i = lines.size() - 1; i > end; i--) {
        char* line = lines[i];
        log_type = static_cast<LogType>(line[0]);
        if (log_type == LogType::WRITE_U) {
            undo_line.transaction_id = get_uint32(1, line);
            // tid in transactions to undo
            std::set<uint32_t>::iterator it = undo_transactions.find(undo_line.transaction_id);
            if (it == undo_transactions.end()) {
                continue;
            }
            
            undo_line.log_type = log_type;
            undo_line.table_id = get_uint32(5, line);
            undo_line.page_num = get_uint32(9, line);
            undo_line.offset = get_uint32(13, line);
            undo_line.len = get_uint32(17, line);
            
            std::cout << i << ": ";
            std::cout << "Undoing Write of transaction: " << undo_line.transaction_id << " on table " << undo_line.table_id << " page " << undo_line.page_num << " offset " << undo_line.offset << " len " << undo_line.len << std::endl;
            FileId file_id = catalog.get_file_id(undo_line.table_id);
            Page& page = buffer_mgr.get_page(file_id, undo_line.page_num);
            page.make_dirty();
            // Write old data
            char* start = page.data() + undo_line.offset;
            char* data = line + 21;
            for (uint32_t j = 0; j < undo_line.len; j++) {
                start[j] = data[j];
            }
            // std::cout << "Data Overwritten Successfully" << std::endl;
            page.unpin();
        }
        break;
    }
}

// Read logfile into a vector of lines of variable length
void read_log(std::ifstream& log_file, std::vector<char*> out) {
    LogType log_type;
    char* log_type_buffer = new char[1];
    char* line_buffer;
    while (log_file.good()) {
        log_file.read(log_type_buffer, 1);
        log_type = static_cast<LogType>(*log_type_buffer);
        switch (log_type) {
            case LogType::START:
            case LogType::ABORT:
            case LogType::COMMIT: {
                line_buffer = new char[5];
                line_buffer[0] = *log_type_buffer;
                log_file.read(line_buffer + 1, 4);
                break;
            }
            case LogType::END_CHKP: {
                line_buffer = new char(1);
                line_buffer[0] = *log_type_buffer;
                break;
            }
            case LogType::START_CHKP: {
                char* t_amount_buffer = new char[4];
                log_file.read(t_amount_buffer, 4);
                uint32_t transactions_amount = static_cast<uint32_t>(*t_amount_buffer);
                line_buffer = new char(5 + 4*transactions_amount);
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
                line_buffer = new char(21 + data_len);
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
            case LogType::WRITE_UR:
            case LogType::END: {
                break;
            }
        }
        out.push_back(line_buffer);
    }
    delete[] log_type_buffer;
}

void delete_lines(std::vector<char*> lines) {
    for (uint32_t i = 0; i < lines.size(); i++) {
        delete[] lines[i];
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

    std::ifstream log_file(log_path, std::ios::binary|std::ios::in);

    if (log_file.fail()) {
        std::cerr << "Could not open the log at path: " << log_path << "\n";
        return EXIT_FAILURE;
    }

    // Read Log
    std::vector<char*> lines;
    read_log(log_file, lines);
    log_file.close();
    // Find things to undo
    std::set<uint32_t> undo_transactions;
    uint32_t truncate_position = get_undo_transactions_and_truncate_position(lines, undo_transactions);

    // Execute undo
    System system = System::init(db_directory, BufferManager::DEFAULT_BUFFER_SIZE);
    undo_after_position(lines, truncate_position, undo_transactions);
    buffer_mgr.flush();
    truncate_log(log_path, truncate_position, lines);
    
    delete_lines(lines);
    return EXIT_SUCCESS;
}
