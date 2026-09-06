import re

with open(r'e:\WhitegramProject\Telegram\SourceFiles\ayu\ui\settings\settings_main.cpp', 'r', encoding='utf-8') as f:
    text = f.read()

# I will find all blocks like:
# .targetSection = something,
# .title = something,
# and swap them!

def replacer(match):
    target_section = match.group(1)
    title = match.group(2)
    return title + '\n\t\t' + target_section

pattern = r'(\.targetSection = [^\n]+)\n\t*(\.title = [^\n]+)'

text = re.sub(pattern, replacer, text)

with open(r'e:\WhitegramProject\Telegram\SourceFiles\ayu\ui\settings\settings_main.cpp', 'w', encoding='utf-8') as f:
    f.write(text)

print("Fixed designated initializers order!")
