import re

def to_snake_case(name):
    s1 = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', name)
    s2 = re.sub('([a-z0-9])([A-Z])', r'\1_\2', s1).lower()

    s2 = s2.replace('2_d', '2d')
    s2 = s2.replace('3_d', '3d')
    s2 = s2.replace('4_d', '4d')
    
    return s2

def sanitize_method_name(name):
    """
    Rename C++ keywords to avoid compilation errors.
    """
    if name == 'new':
        return 'new_'
    if name == 'delete':
        return 'delete_'
    if name == 'default':
        return 'default_'
    if name == 'class':
        return 'class_'
    if name == 'struct':
        return 'struct_'
    if name == 'union':
        return 'union_'
    if name == 'enum':
        return 'enum_'
    if name == 'template':
        return 'template_'
    if name == 'operator':
        return 'operator_'
    return name
