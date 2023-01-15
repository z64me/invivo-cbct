# Sample Invivo case files (.inv)

Some sample Invivo case files are included in this directory.

- [MDHolland](MDHolland-20220721.inv) - an anonymized version of my original CBCT scan
- [UtahTeapot](UtahTeapot-20220905.inv) - a simulated scan of the famous Utah Teapot
- [StanfordBunny](StanfordBunny-20220918.inv) - a similar test using another well-known 3D test model
- [CowSkeleton](CowSkeleton-20220918.inv) - a more complex test involving both bone and soft tissue layers

The last three scans were produced by my software, each constructed from a different image series exported from Blender. [Read about that in more detail here](https://holland.vg/post/digital-cbct-scans/).

For reference, the following commands were used to produce each:

```
bin/result --series 365,100 --invivo bin/release/TeapotUtah-20220905.inv Teapot,Utah,19750228 bin/teapot-512px/%04d.png
bin/result --series 512,1 --invivo bin/release/StanfordBunny-20220918.inv Bunny,Stanford,19941102 bin/bunny-512px/%04d.png
bin/result --series 400,80 --invivo bin/release/CowSkeleton-20220918.inv Skeleton,Cow,20220905 bin/cow-512px/%04d.png
```
