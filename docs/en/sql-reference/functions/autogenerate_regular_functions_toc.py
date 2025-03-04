import json
import os

"""
This script is used to automatically generate a table of contents from the system tables markdown files.
The table of contents is used in index.md.

It is only necessary to run this script if any new system tables .md files have been added to the /system-tables
directory.
"""


def extract_title_and_slug(filename):
    with open(filename, "r") as f:
        lines = f.readlines()

    title, slug = None, None
    for line in lines:
        if line.startswith("title:"):
            title = line.strip().split(": ")[1]
        elif line.startswith("slug:"):
            slug = line.strip().split(": ")[1]
        if title and slug:
            return {"title": title, "slug": slug}

    return None


def main():
    json_array = []
    current_directory = os.getcwd()
    for filename in os.listdir(current_directory):
        if filename.endswith(".md") and filename != "index.md":
            result = extract_title_and_slug(filename)
            if result:
                json_array.append(result)

    json_array = sorted(json_array, key=lambda x: x["title"])

    with open("table_of_contents.json", "w") as f:
        json.dump(json_array, f, indent=4)


if __name__ == "__main__":
    main()
