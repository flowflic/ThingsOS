# Pinyin input data

Original pypinyin 0.55.0 dictionary, MIT licensed. Pinned source hashes and wheel hash are in `../sources.json`.
`python native/generate_ime.py` builds the 413-syllable single-character table (v represents ü).
`phrases_dict.json` and `LICENSE.phrases` are also unchanged data from the same pinned wheel.
`python native/generate_ime_words.py` combines these pronunciations with the MIT jieba word-frequency data to generate 60,000 offline words. Both generators support `--check`.
The independently written IME supports full word spelling, apostrophe disambiguation, remaining-syllable selection, contextual word continuation, UTF-8 cursor insertion and byte limits. Three candidates per page leave room for words up to eight characters. All lookup data is read-only flash; no network or SD dictionary is required.
