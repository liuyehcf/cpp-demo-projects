#include <fstream>
#include <iostream>

#include "person.pb.h"

void savePerson(const tutorial::Person& person, const std::string& filename) {
    std::ofstream output(filename, std::ios::out | std::ios::binary);
    if (!person.SerializeToOstream(&output)) {
        std::cerr << "Failed to write person." << std::endl;
    }
}

tutorial::AnotherPerson loadAnotherPerson(const std::string& filename) {
    tutorial::AnotherPerson person;
    std::ifstream input(filename, std::ios::in | std::ios::binary);
    if (!person.ParseFromIstream(&input)) {
        std::cerr << "Failed to read person." << std::endl;
    }
    return person;
}

int main() {
    tutorial::Person person;
    person.set_id(123);
    person.set_name("John Doe");
    person.set_email("john.doe@example.com");

    const std::string filename = "build/person.data";
    savePerson(person, filename);

    tutorial::AnotherPerson new_person = loadAnotherPerson(filename);

    std::cout << "ID: " << new_person.id() << std::endl;
    std::cout << "Name: " << new_person.name() << std::endl;
    std::cout << "Email: " << new_person.email() << std::endl;

    return 0;
}
