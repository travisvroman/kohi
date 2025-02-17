
#include "xml_parser_tests.h"

#include <defines.h>
#include <parsers/xml_parser.h>
#include <strings/kstring.h>

#include "../expect.h"
#include "../test_manager.h"

const char* test_xml_content =
    "<root>"
    "    <scene id=\"1\">"
    "        <object name=\"Player\" type=\"character\">"
    "            <position>10 20 30</position>"
    "        </object>"
    "        <object name=\"Enemy\" type=\"AI\">"
    "            <position>50 60 70</position>"
    "        </object>"
    "    </scene>"
    "</root>";

u8 xml_parser_should_parse_basic(void) {

    // Check root node
    xml_node* root = xml_parse(test_xml_content);
    expect_should_not_be(0, root);
    expect_string_to_be("root", root->tag);

    // Validate scene node
    const xml_node* scene = xml_child_find(root, "scene");
    {
        expect_should_not_be(0, scene);
        expect_string_to_be("scene", scene->tag);
        const char* id_attr = xml_attribute_get(scene, "id");
        expect_string_to_be("1", id_attr);

        // Validate first object
        const xml_node* obj1 = xml_child_find(scene, "object");
        {
            expect_should_not_be(0, obj1);
            expect_string_to_be("object", obj1->tag);

            const char* name_attr = xml_attribute_get(obj1, "name");
            expect_string_to_be("Player", name_attr);

            const char* type_attr = xml_attribute_get(obj1, "type");
            expect_string_to_be("character", type_attr);

            {
                const xml_node* position = xml_child_find(obj1, "position");
                expect_should_not_be(0, position);
                expect_string_to_be("position", position->tag);

                expect_string_to_be("10 20 30", xml_content_get(position));
            }
        }

        // Validate second object
        {
            const xml_node* obj2 = obj1->next;
            expect_should_not_be(0, obj2);
            expect_string_to_be("object", obj2->tag);

            const char* name_attr = xml_attribute_get(obj2, "name");
            expect_string_to_be("Enemy", name_attr);

            const char* type_attr = xml_attribute_get(obj2, "type");
            expect_string_to_be("AI", type_attr);

            {
                const xml_node* position = xml_child_find(obj2, "position");
                expect_should_not_be(0, position);
                expect_string_to_be("position", position->tag);

                expect_string_to_be("50 60 70", xml_content_get(position));
            }
        }
    }

    xml_free(root);

    return true;
}

void xml_parser_register_tests(void) {
    test_manager_register_test(xml_parser_should_parse_basic, "XML parser should handle basic parsing.");
}
