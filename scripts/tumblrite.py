#!/usr/bin/env python3

import copy
import json
import re
import sys

import requests
from bs4 import BeautifulSoup, Tag

MAX_PAGES = 10
TEMPLATE_HTML = open("template.html").read()


def print_info(severity: str, info: str):
  program_name = sys.argv[0].replace('./', '')
  postfix = f"\x1b[93m{program_name}\x1b[0m: {info}"

  match severity:
    case "ERROR":
      print(f"\x1b[31mERROR\x1b[0m: {postfix}", file=sys.stderr)
    case "WARNING":
      print(f"\x1b[95mWARNING\x1b[0m: {postfix}", file=sys.stderr)
    case "INFO":
      print(f"\x1b[36mINFO\x1b[0m: {postfix}", file=sys.stderr)


def search_page(search_url: str) -> list[str]:
  html = requests.get(search_url).text
  soup = BeautifulSoup(html, features="lxml")

  script_tag = soup.select_one('script[type="application/ld+json"]')
  assert script_tag, "Ooops no google carousel"
  google_carousel = json.loads(script_tag.text)
  return [list_item["url"] for list_item in google_carousel["itemListElement"]]


def search_pages(search_url: str, *, max_pages=MAX_PAGES) -> list[str]:
  fancy_name = search_url[search_url.rindex('/')+1:]
  total_post_urls = []

  # actually more of a while(true). NASA style 0_0
  for i in range(max_pages):
    print_info("INFO", f"Downloading page \x1b[32m{i+1}\x1b[0m for search '{fancy_name}'")
    page_url = search_url if i == 0 else f"{search_url}/page/{i+1}"
    page_posts = search_page(page_url)

    total_post_urls += page_posts
    if len(page_posts) < 10:
      break

  return total_post_urls


def fetch_post(post_url: str) -> dict:
  match = re.match(r'^https://(.*?)\.tumblr\.com/', post_url)
  assert match
  blog_name = match.group(1)

  html = requests.get(post_url).text
  soup = BeautifulSoup(html, features="lxml")

  code_tag = soup.select_one(f'.{blog_name}-npf')
  assert code_tag, "Ooops no json representation"
  return json.loads(code_tag.text)


def format_post_content(npf_content: list[dict], soup: BeautifulSoup, parent: Tag):
  current_list_type: str = "p"
  current_list: Tag = parent

  def append_tag(name: str, string: str = "", **kwargs):
    nonlocal current_list, current_list_type

    tag = soup.new_tag(name, **kwargs)
    if string:
      tag.string = string

    if current_list_type == name:
      current_list.append(tag)
      return

    if current_list_type == "img":
      layout = -len(current_list.contents) % 3
      current_list["class"] += f' img-layout-{layout}'
      parent.append(current_list)

    if current_list_type != "p":
      parent.append(current_list)

    match name:
      case "p":
        current_list = parent
      case "li":
        current_list = soup.new_tag("ol")
      case "img":
        current_list = soup.new_tag("div")
        current_list.attrs["class"] = "tumblr-img-list"

    current_list.append(tag)
    current_list_type = name

  for paragraph in npf_content:
    if "formatting" in paragraph:
      print_info("WARNING", "Can't parse formatting yet...")

    match paragraph:
      case {"type": "text", "text": text, "subtype": "ordered-list-item"}:
        append_tag("li", text)

      case {"type": "text", "text": text, "subtype": subtype}:
        print_info("ERROR", f"Unknown subtype '{subtype}'")
        print(json.dumps(paragraph, indent=2), file=sys.stderr)

      case {"type": "text", "text": text}:
        append_tag("p", text)

      case {"type": "image", "media": media}:
        url = [size["url"] for size in media if size.get("has_original_dimensions")]
        if not url:
          url = [media[0]["url"]]
        append_tag("img", src=url[0], loading="lazy")
        # todo: https://codepen.io/gschier/pen/kyRXVx

      case _:
        print_info("ERROR", f"Unknown paragraph structure")
        print(json.dumps(paragraph, indent=2), file=sys.stderr)

  if current_list_type != "p":
    parent.append(current_list)


def main(search_url: str):
  match = re.match(r'^https://.*?\.tumblr\.com/tagged/(.*)$', search_url)
  assert match
  tag_name = match.group(1).replace('+', ' ').title()

  soup = BeautifulSoup(TEMPLATE_HTML, features="lxml")
  post_list = soup.select_one('*[data-slot="post-list"]')
  assert post_list and post_list.parent

  title_slots = soup.select('*[data-slot="title"]')
  for slot in title_slots:
    slot.string = tag_name

  post_urls = search_pages(search_url)
  for post_url in post_urls:
    post = fetch_post(post_url)
    template = copy.copy(post_list)
    post_slot = template.select_one('*[data-slot="post"]')
    assert post_slot

    format_post_content(post["content"], soup, post_slot)
    post_list.parent.append(template)

  post_list.decompose()
  print(soup.prettify())


if __name__ == '__main__':
  main(sys.argv[1])
