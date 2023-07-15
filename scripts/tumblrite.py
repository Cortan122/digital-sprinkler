#!/usr/bin/env python3

import copy
import json
import re
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from email.utils import parsedate_to_datetime

import requests
from bs4 import BeautifulSoup, Tag

MAX_PAGES = 10
TEMPLATE_HTML = open("template.html").read()
USE_FORMATTING = True
USE_RSS_FOR_SEARCHING = True
# USE_RSS_FOR_POSTS = False
ADD_REBLOG_TRAILS = True

@dataclass
class RssPost:
  url: str
  tags: list[str]
  html: str
  date: datetime

rss_posts_cache: dict[str, RssPost] = {}


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
    case _:
      print(f"\x1b[36m{severity}\x1b[0m: {postfix}", file=sys.stderr)


def search_page(search_url: str) -> list[str]:
  if USE_RSS_FOR_SEARCHING:
    search_url += '/rss'
    xml = requests.get(search_url).text
    soup = BeautifulSoup(xml, features="lxml-xml")
    urls = []
    for item in soup.find_all("item"):
      post = RssPost(
        url=item.find("link").string,
        html=item.find("description").string,
        date=parsedate_to_datetime(item.find("pubDate").string),
        tags=[tag.string for tag in item.find_all("category")],
      )
      rss_posts_cache[post.url] = post
      urls.append(post.url)
    return urls

  html = requests.get(search_url).text
  soup = BeautifulSoup(html, features="lxml")

  script_tag = soup.select_one('script[type="application/ld+json"]')
  assert script_tag, "Ooops no google carousel"
  google_carousel = json.loads(script_tag.text)
  return [list_item["url"] for list_item in google_carousel["itemListElement"]]


def search_pages(search_url: str, *, max_pages=MAX_PAGES) -> list[str]:
  if re.match(r'^https://.*?\.tumblr\.com/post/', search_url):
    return [search_url]

  fancy_name = search_url[search_url.rindex('/')+1:]
  total_post_urls = []

  # actually more of a while(true). NASA style 0_0
  for i in range(max_pages):
    print_info("INFO", f"Downloading page \x1b[32m{i+1}\x1b[0m for search '{fancy_name}'")
    page_url = search_url if i == 0 else f"{search_url}/page/{i+1}"
    page_posts = search_page(page_url)

    total_post_urls += page_posts
    if len(page_posts) < 15:
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


def append_formatted_text(soup: BeautifulSoup, parent: Tag, text: str, formatting: list[dict]):
  prev = 0
  stack = [parent]
  end_stack = [100_000_000]

  def pop_tag():
    nonlocal prev

    next_end = end_stack[-1]
    stack[-1].append(text[prev:next_end])
    stack[-2].append(stack[-1])
    stack.pop()
    end_stack.pop()
    prev = next_end

  def push_tag(tag: Tag, start: int, end: int):
    nonlocal prev

    while end_stack[-1] <= start:
      pop_tag()
    stack[-1].append(text[prev:start])

    assert end <= end_stack[-1], "Improperly nested styles"
    tag.attrs["class"] = "tumblr-formatting"
    stack.append(tag)
    end_stack.append(end)
    prev = start

  for span in formatting:
    match span:
      case {"start": start, "end": end, "type": "bold"}:
        push_tag(soup.new_tag('b'), start, end)
      case {"start": start, "end": end, "type": "small"}:
        push_tag(soup.new_tag('small'), start, end)
      case {"start": start, "end": end, "type": "italic"}:
        push_tag(soup.new_tag('i'), start, end)
      case {"start": start, "end": end, "type": "strikethrough"}:
        push_tag(soup.new_tag('s'), start, end)

      case {"start": start, "end": end, "type": "mention", "blog": blog}:
        push_tag(soup.new_tag('a', href=f'https://www.tumblr.com/{blog["name"]}'), start, end)
      case {"start": start, "end": end, "type": "link", "url": url}:
        push_tag(soup.new_tag('a', href=url), start, end)
      case {"start": start, "end": end, "type": "color", "hex": hex}:
        push_tag(soup.new_tag('span', style=f'color: {hex};'), start, end)

      case {"start": start, "end": end, "type": type}:
        print_info("WARNING", f"Unknown formatting type '{type}'")

  while len(stack) > 1:
    pop_tag()
  stack[-1].append(text[prev:])


def format_post_content(npf_content: list[dict], soup: BeautifulSoup, parent: Tag):
  current_list_type: str = "p"
  current_list: Tag = parent

  def append_tag(name: str, string: str = "", formatting: list[dict] | None = None, **kwargs):
    nonlocal current_list, current_list_type

    tag = soup.new_tag(name, **kwargs)
    if formatting and string:
      append_formatted_text(soup, tag, string, formatting)
    elif string:
      tag.string = string

    if name not in {"li", "img"}:
      name = "p"

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
      case _:
        assert False, "Should be unreachable"

    current_list.append(tag)
    current_list_type = name

  for paragraph in npf_content:
    formatting = paragraph.get("formatting") if USE_FORMATTING else None

    match paragraph:
      case {"type": "text", "text": text, "subtype": "ordered-list-item"}:
        append_tag("li", text, formatting)

      case {"type": "text", "text": text, "subtype": "heading1"}:
        append_tag("h2", text, formatting)
      case {"type": "text", "text": text, "subtype": "heading2"}:
        append_tag("h3", text, formatting)

      case {"type": "text", "text": text, "subtype": subtype}:
        print_info("ERROR", f"Unknown subtype '{subtype}'")
        print(json.dumps(paragraph, indent=2), file=sys.stderr)

      case {"type": "text", "text": text}:
        append_tag("p", text, formatting)

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


def init_template_soup(search_url: str) -> tuple[BeautifulSoup, Tag]:
  match = re.match(r'^https://.*?\.tumblr\.com/(?:tagged|post/[0-9]+)/(.*)$', search_url)
  assert match
  tag_name = match.group(1).replace('+', ' ').replace('-', ' ').title()

  soup = BeautifulSoup(TEMPLATE_HTML, features="lxml")
  post_list = soup.select_one('*[data-slot="post-list"]')
  assert post_list and post_list.parent

  title_slots = soup.select('*[data-slot="title"]')
  for slot in title_slots:
    slot.string = tag_name

  return soup, post_list


def format_reblog_header(soup: BeautifulSoup, parent: Tag, blog: dict, post_id: str):
  div = soup.new_tag("div", **{"class": "tumblr-blog"})
  ava_url = blog["avatar"][0]["url"]
  ava = soup.new_tag("img", src=ava_url, **{"class": "tumblr-ava"})
  div.append(ava)

  name = soup.new_tag("a", href=blog["url"], **{"class": "tumblr-blogname"})
  name.string = blog["name"]
  div.append(name)

  post_url = f'https://www.tumblr.com/{blog["name"]}/{post_id}/'
  postlink = soup.new_tag("a", href=post_url, **{"class": "tumblr-postlink"})
  div.append(postlink)

  parent.append(div)


def format_post(soup: BeautifulSoup, parent: Tag, post: dict):
  for reblog in post.get("trail", []):
    format_post(soup, parent, reblog)
    divider = soup.new_tag("hr", **{"class": "tumblr-divider"})
    parent.append(divider)

  if post.get("blog") and ADD_REBLOG_TRAILS:
    format_reblog_header(soup, parent, post["blog"], post["post"]["id"])

  format_post_content(post["content"], soup, parent)


def fetch_posts(soup: BeautifulSoup, post_list: Tag, post_urls: list[str]):
  assert post_list.parent

  for post_url in post_urls:
    template = copy.copy(post_list)
    post_slot = template.select_one('*[data-slot="post"]')
    assert post_slot

    match = re.match(r'^https://.*?\.tumblr\.com/post/([0-9]+)', post_url)
    if match:
      post_slot.attrs["id"] = f'post-{int(match.group(1))}'
    else:
      print_info("ERROR", f"can't find post id in url '{post_url}'")

    post = fetch_post(post_url)
    format_post(soup, post_slot, post)
    post_list.parent.append(template)


def main(search_url: str):
  soup, post_list = init_template_soup(search_url)

  start = time.time()
  post_urls = search_pages(search_url)
  end = time.time()
  print_info("INFO", f"search_pages took {end - start:.4f} seconds")

  fetch_posts(soup, post_list, post_urls)
  end2 = time.time()
  print_info("INFO", f"fetch_posts took {end2 - end:.4f} seconds")

  post_list.decompose()
  print(soup)


if __name__ == '__main__':
  main(sys.argv[1])
