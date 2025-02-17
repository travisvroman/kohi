#pragma once

#include "defines.h"

typedef struct xml_attribute {
    // The name of the attribute.
    const char* key;
    // The value of the attribute.
    const char* value;
    // The next attribute in the linked list.
    struct xml_attribute* next;
} xml_attribute;

typedef struct xml_node {
    // The tag name of the node.
    const char* tag;
    // The string content of the element, if it exists.
    const char* content;
    // Linked list of attributes, if they exist.
    xml_attribute* attributes;
    // Linked list of children, if they exist.
    struct xml_node* children;
    // The next node in the linked list.
    struct xml_node* next;
} xml_node;

/**
 * @brief Parses the provided source string into a tree containing XML data.
 *
 * @param source_str The source string to parse.
 *
 * @return A pointer to a node containing the xml tree.
 */
KAPI xml_node* xml_parse(const char* source_str);

/**
 * @brief Frees the given XML node recursively.
 *
 * @param node A pointer to the node to free.
 */
KAPI void xml_free(xml_node* node);

/**
 * @brief Finds and returns the first child node with the given tag.
 *
 * @param parent A constant pointer to the parent node to search.
 * @param tag The name of the tag to find.
 *
 * @return A constant pointer to the node, if found; otherwise 0.
 */
KAPI const xml_node* xml_child_find(const xml_node* parent, const char* tag);

/**
 * @brief Gets the content of the given XML node.
 *
 * @param node A constant pointer to the node whose value to retrieve.
 * @return A string containing the node's content if it exists; otherwise 0
 */
KAPI const char* xml_content_get(const xml_node* node);

/**
 * @brief Gets an attribute with the given key from the given XML node.
 *
 * @param node A constant pointer to the node whose attribute to retrieve.
 * @param key A string containing the attribute's name.
 *
 * @return A string containing the attribute's content if it exists; otherwise 0
 */
KAPI const char* xml_attribute_get(const xml_node* node, const char* key);
