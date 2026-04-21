import xml.etree.ElementTree as ET
import json

def parse_asycuda_xml_to_dict(element):
    """Recursively converts XML elements to a dictionary with ASYCUDA specific rules."""
    
    # 1. Handle explicit <null/> tags
    if len(element) == 1 and element[0].tag == 'null':
        return None
        
    # 2. Base case: The element has no children
    if len(element) == 0:
        return element.text.strip() if element.text else ""

    # 3. Recursive case: The element has children
    result = {}
    for child in element:
        child_data = parse_asycuda_xml_to_dict(child)
        
        # If the tag already exists, convert it to a list (Array)
        if child.tag in result:
            if isinstance(result[child.tag], list):
                result[child.tag].append(child_data)
            else:
                result[child.tag] = [result[child.tag], child_data]
        else:
            result[child.tag] = child_data

    return result

# Load and parse the file
try:
    tree = ET.parse('template.xml')
    root = tree.getroot()
    
    asycuda_dict = {root.tag: parse_asycuda_xml_to_dict(root)}
    
    # Save to a new JSON file
    with open('asycuda_output.json', 'w', encoding='utf-8') as f:
        json.dump(asycuda_dict, f, indent=2, ensure_ascii=False)
        
    print("Successfully converted template.xml to asycuda_output.json")

except Exception as e:
    print(f"Error parsing XML: {e}")