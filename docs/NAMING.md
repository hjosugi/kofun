# Naming

## Working title

このprototypeでは`Cofn`をworking titleとして使う。

意図:

- Rustへのcofnrationを出発点にする
- functional + Rustを連想しやすい
- CLI名が短い
- `.cofn` extensionを使いやすい

## Collision risk

`cofn`という名前はすでにRust ecosystemのcrate名として使われている。また、過去に「functional Rust」の略として同じ発想が公開された例もある。

したがって、public launch前に次を必須とする。

1. crates.io、PyPI、npm、GitHub、GitLab、主要Linux distributionを検索する。
2. programming language、compiler、database、developer toolの商標を調査する。
3. domain、social handle、package namespaceを確認する。
4. 日本、米国、EUを最低限の対象として法律専門家に確認する。
5. project名、CLI名、package prefix、file extensionを別々に評価する。

## Candidate direction

最終名称は、次を満たす短い造語を優先する。

- 4〜7文字
- 日本語話者と英語話者が発音しやすい
- `fn`やpipelineの軽さを連想できる
- Rust派生言語と誤認させすぎない
- searchabilityが高い
- package namespaceを確保できる

このZIPでは名前を固定せず、compiler内部のnamespace変更を一括実行できるようにする。
