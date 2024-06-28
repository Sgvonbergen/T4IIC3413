#include <fstream>
#include <iostream>

#include "relational_model/system.h"
#include "third_party/cli11/CLI11.hpp"

int main() {
    System system = System::init("./examples/test_p1_simple_undo", BufferManager::DEFAULT_BUFFER_SIZE);
    FileId file_id = catalog.get_file_id(1);
    Page& page = buffer_mgr.get_page(file_id, 0);
    page.make_dirty();
    for (size_t i = 0; i < Page::SIZE; i++) {
        page.data()[i] = 0;
    }
    page.unpin();
    file_mgr.flush(page);
    std::cout << "Page 0 of table 1 zeroed" << std::endl;
    buffer_mgr.flush();
}
