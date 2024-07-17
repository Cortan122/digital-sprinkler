#!/usr/bin/env python3

import csv
import re
import sys
from datetime import datetime
from os.path import basename
from typing import Collection, Literal
from xml.dom.minidom import Document, Element

ColumnType = Literal['image', 'url', 'bool', 'int', 'float', 'text', 'duration', 'youtube', 'date']
CsvTable = tuple[list[str], list[list[str]], list[ColumnType]]
LINK_TEXT = 'mal'
HIDDEN_COLUMNS = ['id', 'title', 'track']
DOUBLE_COUNT_COLUMNS = ['dub', 'sub']
INDEX_COLUMN = 'index'

CSS = '''
table {
  font-family: arial, sans-serif;
  border-collapse: collapse;
  width: 100%;
  text-align: left;
}

td, th {
  border: 1px solid #dddddd;
  text-align: left;
  padding: 8px;
}

tr:nth-child(even) {
  background-color: #dddddd;
}

a {
  color: #000;
  text-decoration: none;
}

a:hover {
  color: #0000ee;
  text-decoration: underline;
}

td:hover {
  --poster-height: auto;
  --poster-position: absolute;
}

img.poster {
  height: var(--poster-height, 18px);
  position: var(--poster-position, static);
  pointer-events: none;
  vertical-align: bottom;
}

input {
  vertical-align: bottom;
}

body {
  margin-bottom: 300px;
}

div.total {
  font-weight: bold;
  margin-top: 2rem;
}

a.youtube {
  font-family: monospace;
}
'''


def create_html_doc(css: str = '', title: str = '') -> tuple[Document, Element]:
    # Create a new XML document
    doc = Document()

    # Create the root element (an HTML document)
    html = doc.createElement('html')
    doc.appendChild(html)

    # Create the `head` element
    head = doc.createElement('head')
    html.appendChild(head)

    # Create the `style` element
    style = doc.createElement('style')
    head.appendChild(style)
    style.appendChild(doc.createTextNode(css))

    # Create the `title` element
    if title:
        title_el = doc.createElement('title')
        head.appendChild(title_el)
        title_el.appendChild(doc.createTextNode(title))

    # Create the `meta` element
    meta = doc.createElement('meta')
    head.appendChild(meta)
    meta.setAttribute('charset', 'utf-8')

    # Create the `body` element
    body = doc.createElement('body')
    html.appendChild(body)

    return doc, body


def guess_type(column: Collection[str], header: str) -> ColumnType:
    if all(re.match(r'^https?://', cell) for cell in column):
        if all(re.search(r'\.(jpe?g|png|webp|gif|bmp)$', cell) for cell in column):
            return "image"
        else:
            return "url"
    elif all(re.match(r'^[01]?$', cell) for cell in column):
        return "bool"
    elif all(re.match(r'^-?[0-9]+$', cell) for cell in column):
        if header.lower().strip() == "duration":
            return "duration"
        else:
            return "int"
    elif all(re.match(r'^-?[0-9]+\.[0-9]+$', cell) for cell in column):
        return "float"
    elif all(re.match(r'^[0-9A-Za-z_\-]{11}$', cell) for cell in column):
        if "youtube" in header.lower() or "yt" in header.lower():
            return "youtube"
        else:
            return "text"
    elif all(re.match(r'^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$', cell) for cell in column):
        return "date"
    else:
        return "text"


def parse_csv(csv_filename: str) -> CsvTable:
    # Open the CSV file for reading
    with open(csv_filename, 'r') as csv_file:
        # Create a CSV reader object
        reader = csv.reader(csv_file, delimiter=',')

        # Get the first row of the CSV file (the column headers)
        headers = next(reader)
        rows = list(reader)

        # Initialize a list to store the data types for each column
        column_types = []

        # Iterate over the columns
        for i, column in enumerate(zip(*rows)):
            column_types.append(guess_type(column, headers[i]))

        return headers, rows, column_types


def write_html(doc: Document, html_filename: str) -> None:
    # Open the HTML file for writing
    with open(html_filename, 'w') as html_file:
        # Write the `DOCTYPE` tag
        html_file.write('<!DOCTYPE html>\n')
        # Write the XML document to the HTML file
        html_file.write(doc.toprettyxml(indent='  '))


def create_table(
    doc: Document,
    data: Collection[Collection[str | Element]],
    headers: list[str] | None = None
) -> Element:
    """Create an HTML table element from a 2d list of strings or minidom elements."""
    # Create the table element
    table = doc.createElement('table')

    # Write the column headers as a row in the HTML table
    if headers:
        tr = doc.createElement('tr')
        table.appendChild(tr)
        for header in headers:
            th = doc.createElement('th')
            tr.appendChild(th)
            th.appendChild(doc.createTextNode(header))

    # Iterate over the rows in the data
    for row in data:
        # Create a table row element
        tr = doc.createElement('tr')
        table.appendChild(tr)
        # Iterate over the cells in the row
        for cell in row:
            # Create a table cell element
            td = doc.createElement('td')
            tr.appendChild(td)
            # If the cell is a string, create a text node and add it to the table cell element
            if isinstance(cell, str):
                td.appendChild(doc.createTextNode(cell))
            # If the cell is a minidom element, add it to the table cell element
            elif isinstance(cell, Element):
                td.appendChild(cell)

    # Return the table element
    return table


def humanize_duration(milliseconds: int, use_days=True) -> str:
    seconds = milliseconds // 1000
    if use_days:
        days, seconds = divmod(seconds, 3600*24)
    else:
        days = 0
    hours, seconds = divmod(seconds, 3600)
    minutes, seconds = divmod(seconds, 60)

    parts = []
    if days:
        parts.append(f'{days} day{"s" if days > 1 else ""}')
    if hours:
        parts.append(f'{hours} hour{"s" if hours > 1 else ""}')
    if minutes:
        parts.append(f'{minutes} minute{"s" if minutes > 1 else ""}')

    return ', '.join(parts)


def format_cell(doc: Document, cell: str, cell_type: ColumnType) -> Element | str:
    """Format a single table cell to the desired type."""

    # Format the cell based on the desired type
    if cell_type == 'url':
        # If the cell is a URL, create an anchor element and return it
        a = doc.createElement('a')
        a.setAttribute('href', cell)
        a.appendChild(doc.createTextNode(LINK_TEXT))
        return a
    elif cell_type == 'image':
        # If the cell is an image, create an image element and return it
        img = doc.createElement('img')
        img.setAttribute('class', 'poster')
        img.setAttribute('src', cell)
        return img
    elif cell_type == 'duration':
        # If the cell is a duration, format the duration as a string and return it as text
        return humanize_duration(int(cell))
    elif cell_type == 'bool':
        # If the cell is a boolean, create an input element and return it
        input_el = doc.createElement('input')
        input_el.setAttribute('type', 'checkbox')
        if cell == '1':
            input_el.setAttribute('checked', 'True')
        return input_el
    elif cell_type == 'float':
        # If the cell is a float, format it to only have 3 digits of precision
        return f'{float(cell):.3f}'
    elif cell_type == 'youtube':
        # If the cell is a youtube id, format it as a URL
        a = doc.createElement('a')
        a.setAttribute('class', 'youtube')
        a.setAttribute('href', f'https://www.youtube.com/watch?v={cell}')
        a.appendChild(doc.createTextNode(cell))
        return a
    elif cell_type == 'date':
        # If the cell is a date, format the date as an iso type sting
        return datetime.fromisoformat(cell).strftime("%Y-%m-%d")
    else:
        # For all other cell types, treat the cell value as a text node
        return cell


def format_table(doc: Document, table: CsvTable) -> tuple[list[str], list[list[Element | str]]]:
    headers, data, types = table

    formatted = [
        [format_cell(doc, cell, types[i]) for i, cell in enumerate(row)
         if headers[i] not in HIDDEN_COLUMNS]
        for row in data
    ]
    headers = [header for header in headers if header not in HIDDEN_COLUMNS]

    # Move the index column to the front
    if INDEX_COLUMN in headers and headers.index(INDEX_COLUMN):
        index_pos = headers.index(INDEX_COLUMN)
        for row in formatted:
            row.insert(0, row.pop(index_pos))
        headers.pop(index_pos)
        headers.insert(0, INDEX_COLUMN + '\u00A0◣')

    return headers, formatted


def total_duration(table: CsvTable) -> int | None:
    headers, data, types = table

    if not any(typ == "duration" for typ in types):
        return None

    return sum(
        int(cell) * max(1, sum(
            cell == '1'
            for head, typ, cell in zip(headers, types, row)
            if typ == "bool" and head in DOUBLE_COUNT_COLUMNS
        ))
        for row in data
        for typ, cell in zip(types, row)
        if typ == "duration"
    )


def csv_to_html(csv_filename: str, html_filename: str) -> None:
    # Parse the CSV file for reading
    table = parse_csv(csv_filename)

    # Create a new HTML document
    doc, body = create_html_doc(CSS, f"table for {basename(csv_filename)}")

    # Format the table data and headers
    headers, formatted = format_table(doc, table)

    # Create the root element (an HTML table)
    html_table = create_table(doc, formatted, headers)
    body.appendChild(html_table)

    # Calculate and add the total duration to the HTML document
    total = total_duration(table)
    if total:
        duration = humanize_duration(total, use_days=False)
        div = doc.createElement('div')
        body.appendChild(div)
        div.setAttribute("class", "total")
        div.appendChild(doc.createTextNode(duration))

    write_html(doc, html_filename)


def main():
    csv_to_html(sys.argv[1], sys.argv[2])


if __name__ == '__main__':
    main()
