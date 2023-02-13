# Digital Sprinkler

*A tool to help grow your digital garden*

It helps you make your own website!! (kinda) \
You might have a lot of different stuff to show to the world, but it's all stored in a bunch of different places.
This tool will look for your stuff all over the web (i.e. it will pull your urls every 2 hours), and then run it through some preprocessor script (or [filter](/scripts/)).
This way you txt files can look all fancy like!

But, for now, its all very unfinished and can only deal with git urls.
I plan to come up with a new recursive syntax for my config files, so we don't have to write "git@github.com" a thousand times.
Also i probably shouldn't (ab)use the git binary to pull half broken repositories.

## Build & Run

```console
$ make
$ ./sprinkler --help
```

## Available filters

1. `copy` — Self explanatory. Just copies the file...
2. `text2html.py` — Converts txt files into fancy html, styled like the Mariana color scheme form [Sublime Text](https://www.sublimetext.com/).
3. `animetable.py` — Converts a csv table to html. Can theoretically be used on any table, but has some hardcoded variables for my anime list.
4. `posterwall.sh` — Finds image urls in a text file and arranges them into a giant png.
5. `latex.sh` — Compiles latex code into pdf. Untested...

## The name

The name "digital sprinkler" is a stupid pun, because some people call personal websites "digital gardens" i guess...

I'm not that well versed in the terminology, but i don't like perfectly sterile looking wordpress websites.
I'm more of a fan of the "neocities" or "js/css is for losers" aesthetics.

## Todo list

- [ ] Implement a new config format, instead of just using tsv
- [ ] Pass custom css to filters
- [ ] Use a custom implementation of the "git over ssh" (or whatever it's called) algorithm
