#include <Poco/DOM/AutoPtr.h>
#include <Poco/DOM/DOMImplementation.h>
#include <Poco/DOM/DOMWriter.h>
#include <Poco/DOM/Document.h>
#include <Poco/DOM/Element.h>
#include <Poco/DOM/Text.h>
#include <Poco/StreamCopier.h>
#include <Poco/URI.h>
#include <Poco/XML/XMLWriter.h>

#include <iostream>

int main() {
    Poco::AutoPtr<Poco::XML::Document> doc = new Poco::XML::Document();

    Poco::AutoPtr<Poco::XML::Element> configuration = doc->createElement("configuration");
    doc->appendChild(configuration);

    {
        Poco::AutoPtr<Poco::XML::Element> property = doc->createElement("property");
        Poco::AutoPtr<Poco::XML::Element> name = doc->createElement("name");
        Poco::AutoPtr<Poco::XML::Text> name_text = doc->createTextNode("fs.defaultFS");
        name->appendChild(name_text);
        property->appendChild(name);
        Poco::AutoPtr<Poco::XML::Element> value = doc->createElement("value");
        Poco::AutoPtr<Poco::XML::Text> value_text = doc->createTextNode("hdfs://haruna");
        value->appendChild(value_text);
        property->appendChild(value);
        configuration->appendChild(property);
    }
    {
        Poco::AutoPtr<Poco::XML::Element> property = doc->createElement("property");
        Poco::AutoPtr<Poco::XML::Element> name = doc->createElement("name");
        Poco::AutoPtr<Poco::XML::Text> name_text = doc->createTextNode("dfs.nameservices");
        name->appendChild(name_text);
        property->appendChild(name);
        Poco::AutoPtr<Poco::XML::Element> value = doc->createElement("value");
        Poco::AutoPtr<Poco::XML::Text> value_text = doc->createTextNode("haruna");
        value->appendChild(value_text);
        property->appendChild(value);
        configuration->appendChild(property);
    }

    Poco::XML::DOMWriter writer;
    writer.setOptions(Poco::XML::XMLWriter::PRETTY_PRINT);
    writer.writeNode(std::cout, doc);
    return 0;
}
