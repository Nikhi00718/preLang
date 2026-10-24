# RQ6: real data-preparation bugs from GitHub issues

- Qualifying issues: 44 (from 17 error signatures; 91 more results were read and did not qualify)
- Reported before any data is processed: 18 of 44 (41%): 12 with no annotation, 6 once the requirement is declared with EXPECT
- Cannot occur in a PrepLang program: 3; not covered: 23
- Before the two fixes the study led to (format from file content, UTF-8 check): 15 of 44 (34%)
- Reproductions checked against the compiler: 21 of 21 pass

## Why the others are not covered

| Reason | Issues |
|---|---:|
| input format | 6 |
| operation not in the language | 5 |
| dates | 4 |
| per-image label files | 3 |
| input produced during the run | 2 |
| no suitable contract | 2 |
| model code | 1 |

## Issues

| Sig. | Issue | Root cause | Verdict | Before fixes | Compiler says (reproduction) |
|---|---|---|---|---|---|
| S1 | [JayMan146/PersonalHistory#1](https://github.com/JayMan146/PersonalHistory/issues/1) | edited HEIC photos with transparency converted to JPEG | none | none | input format: HEIC |
| S1 | [cocktailpeanut/fluxgym#109](https://github.com/cocktailpeanut/fluxgym/issues/109) | screenshots named .jpg are PNG images with transparency; the dataset step saves them as JPEG | compile | compile | ✓ JPG cannot store transparency, but 1 image(s) have 4 channels / hint: add CONVERT dataset TO PNG; or GRAYSCALE dataset; |
| S1 | [JoshData/pdf-diff#44](https://github.com/JoshData/pdf-diff/issues/44) | a rendered PDF page with transparency saved as JPEG | none | none | input format: PDF |
| S2 | [gogoczh/CoMT#1](https://github.com/gogoczh/CoMT/issues/1) | a damaged image file in a downloaded dataset | compile | compile | ✓ 1 of 3 files in images/ cannot be decoded / note: 0003.jpg: not a JPEG file (bad signature) / hint: add REMOVE_CORRUPT images; before line 2 |
| S2 | [piotrantosz/google-arts-crawler#22](https://github.com/piotrantosz/google-arts-crawler/issues/22) | a downloaded tile is a 404 text page saved as .jpg | compile | compile | ✓ 1 of 3 files in blobs/ cannot be decoded / note: 1354.jpg: not a JPEG file (bad signature) / hint: add REMOVE_CORRUPT tiles; before line 3 |
| S2 | [bbengfort/inigo#3](https://github.com/bbengfort/inigo/issues/3) | a photo folder also holds Photoshop .psd files | design | design | ✓ compiled and ran; 3 output file(s) |
| S3 | [PacktPublishing/Advanced-Deep-Learning-with-Python#2](https://github.com/PacktPublishing/Advanced-Deep-Learning-with-Python/issues/2) | the image file to load was never created | compile | compile | ✓ cannot open image file 'images/dog.jpg' / hint: LOAD_IMAGE expects one file; use LOAD_IMAGES for a folder |
| S3 | [Gogul09/image-classification-python#11](https://github.com/Gogul09/image-classification-python/issues/11) | file names are built as 1..N but the files are named differently | design | design | ✓ compiled and ran; 4 output file(s) |
| S3 | [HullLab/AutoMorph#11](https://github.com/HullLab/AutoMorph/issues/11) | a height map written by an external tool in the same run was never written | none | none | input produced during the run: by an external tool |
| S4 | [xinntao/ESRGAN#145](https://github.com/xinntao/ESRGAN/issues/145) | float model output written with imwrite | none | none | input produced during the run: by a model |
| S5 | [linjx-ustc1106/TuiGAN-PyTorch#3](https://github.com/linjx-ustc1106/TuiGAN-PyTorch/issues/3) | one training folder has grayscale images; the model needs 3 channels | contract | contract | ✓ contract violated: 2 of 2 images in train_a break CHANNELS 3 / note: 1 channel: 2 images / note: a1.png: 1 channel / note: a2.png: 1 channel / hint: use GRAYSCALE, or fix the files |
| S5 | [reckdk/SkelCompletion-3D#3](https://github.com/reckdk/SkelCompletion-3D/issues/3) | 3-channel NumPy volumes for a 1-channel model | none | none | input format: NumPy volumes |
| S5 | [kevinoop0/WaveCT-AIN_base_version#1](https://github.com/kevinoop0/WaveCT-AIN_base_version/issues/1) | one content image is grayscale; the model needs 3 channels | contract | contract | ✓ contract violated: 1 of 2 images in content break CHANNELS 3 / note: 1 channel: 1 image / note: 3 channels: 1 image / note: 2.png: 1 channel / hint: use GRAYSCALE, or fix the files |
| S6 | [Suanmd/TR-MISR#7](https://github.com/Suanmd/TR-MISR/issues/7) | images of different sizes (32x32 and 32x64) in one batch | none | none | no suitable contract: equal image sizes (a RESIZE would prevent it) |
| S6 | [bshall/hubert#3](https://github.com/bshall/hubert/issues/3) | audio files not resampled to 16 kHz | none | none | input format: audio |
| S6 | [jingyuanli001/RFR-Inpainting#47](https://github.com/jingyuanli001/RFR-Inpainting/issues/47) | some training images have 4 channels (RGBA) | contract | contract | ✓ contract violated: 1 of 3 images in train break CHANNELS 3 / note: 3 channels: 2 images / note: 4 channels: 1 image / note: 2.png: 4 channels / hint: use GRAYSCALE, or fix the files |
| S7 | [WongKinYiu/PyTorch_YOLOv4#376](https://github.com/WongKinYiu/PyTorch_YOLOv4/issues/376) | image and label paths do not match | none | none | per-image label files: one text file per image, not a table |
| S7 | [201918010332Thomas/ANPR#1](https://github.com/201918010332Thomas/ANPR/issues/1) | image and label paths do not match | none | none | per-image label files: one text file per image, not a table |
| S7 | [MrFahad/YOLOv7-Segmentation-and-Tracking#2](https://github.com/MrFahad/YOLOv7-Segmentation-and-Tracking/issues/2) | 860 label files are missing | none | none | per-image label files: one text file per image, not a table |
| S8 | [ActivitySim/populationsim#189](https://github.com/ActivitySim/populationsim/issues/189) | a crosswalk file lists zones that the group-quarters table lacks | contract | contract | ✓ contract violated: 3 of 6 values of crosswalk.TAZ have no match in gq.TAZ / note: row 2: 2 / note: row 4: 4 / note: row 6: 6 |
| S8 | [RoversX/umami-csv-import-script#1](https://github.com/RoversX/umami-csv-import-script/issues/1) | a new export format dropped two columns the script selects | compile | compile | ✓ column 'subdivision1' does not exist in 'events' / hint: columns of 'events': session_id, hostname, browser, os, country, region |
| S8 | [PacktPublishing/Pandas-Cookbook-Second-Edition#6](https://github.com/PacktPublishing/Pandas-Cookbook-Second-Edition/issues/6) | a group with one row breaks index arithmetic after groupby | none | none | operation not in the language: groupby |
| S9 | [jyyulab/SJARACNe#40](https://github.com/jyyulab/SJARACNe/issues/40) | gene symbols in a column that must hold numbers | compile | compile | ✓ NORMALIZE needs a numeric column, but expression.value_1 is STRING / hint: 'value_1' is STRING because line 4 has "RPL10" |
| S9 | [NaegleLab/KSTAR#73](https://github.com/NaegleLab/KSTAR/issues/73) | a text accession column is treated as a numeric weight column | compile | compile | ✓ NORMALIZE needs a numeric column: 1 of 3 columns matched by 'network.*' break it / note: network.accession : STRING   <-- breaks the rule / note: network.net_1 : FLOAT [0.3, 0.8]   ok / note: network.net_2 : FLOAT [0.1, 0.9]   ok / hint: narrow the selector, e.g. WHERE TYPE NUMERIC or EXCEPT [...] |
| S9 | [CCMS-UCSD/GNPSDocumentation#431](https://github.com/CCMS-UCSD/GNPSDocumentation/issues/431) | the text 'unknonw' in a numeric column of a quantification table | compile | compile | ✓ cannot compare STRING column 'mz' with number 100 / hint: 'mz' is STRING because line 3 has "unknonw" |
| S10 | [MLJejuCamp2017/clothing-recommender#6](https://github.com/MLJejuCamp2017/clothing-recommender/issues/6) | a category column holds text and is compared with a number | compile | compile | ✓ cannot compare STRING column 'category' with number 21 / hint: 'category' is STRING because line 4 has "category_label" |
| S10 | [rhiever/datacleaner#16](https://github.com/rhiever/datacleaner/issues/16) | a column mixes numbers and text, so sorting its values fails | design | design | ✓ compiled and ran; 2 output file(s) |
| S12 | [salsiza/junction25#2](https://github.com/salsiza/junction25/issues/2) | the prediction table lacks columns the model was trained on | contract | contract | ✓ contract violated: predict and train have different schemas / note: avg_order_size is in train but not in predict / note: avg_purchase_size is in train but not in predict / hint: list columns that may differ in EXCEPT [...] |
| S12 | [ahmed1996said/dubai-housing-prices#3](https://github.com/ahmed1996said/dubai-housing-prices/issues/3) | one-hot encoding gives different columns for training and test data | none | none | operation not in the language: one-hot encoding |
| S12 | [microsoft/FLAML#1480](https://github.com/microsoft/FLAML/issues/1480) | a date column is handled differently at fit and predict time | none | none | dates |
| S13 | [quantifyearth/topogmesh#1](https://github.com/quantifyearth/topogmesh/issues/1) | NaN written into an integer elevation raster | none | none | input format: GeoTIFF |
| S13 | [geospace-code/georinex#110](https://github.com/geospace-code/georinex/issues/110) | a navigation file has missing week numbers | none | none | input format: RINEX |
| S14 | [lcmchris/thesillyhome-addon-repo#32](https://github.com/lcmchris/thesillyhome-addon-repo/issues/32) | duplicate (time, entity) pairs make a pivot fail | none | none | operation not in the language: pivot, uniqueness of column pairs |
| S14 | [thepycoach/python-course-for-excel-users#4](https://github.com/thepycoach/python-course-for-excel-users/issues/4) | duplicate (year, country) pairs make a pivot fail | none | none | operation not in the language: pivot, uniqueness of column pairs |
| S14 | [ecmwf/anemoi-datasets#760](https://github.com/ecmwf/anemoi-datasets/issues/760) | duplicate rows for an index and pivot key | none | none | operation not in the language: pivot, uniqueness of column pairs |
| S15 | [ooni/data#164](https://github.com/ooni/data/issues/164) | a downloaded JSON dataset changed a date field to 'latest' | none | none | dates: the input is also JSON |
| S15 | [sandsturm/whatsapp-converter#14](https://github.com/sandsturm/whatsapp-converter/issues/14) | a chat export uses another date format | none | none | dates |
| S15 | [cdump/investments#50](https://github.com/cdump/investments/issues/50) | yearly reports without operations give empty dates | none | none | dates |
| S16 | [Faisalhakimi22/github-issue-classifier#1](https://github.com/Faisalhakimi22/github-issue-classifier/issues/1) | a CSV file saved as Windows-1252 | compile | none | ✓ line 3 of 'issues.csv' is not UTF-8 text (byte 0xE9) / hint: the file was probably saved as Windows-1252 or Latin-1; save it as UTF-8 (in Excel: Save As > CSV UTF-8) |
| S16 | [austinbcomstock/buildium-to-stessa#1](https://github.com/austinbcomstock/buildium-to-stessa/issues/1) | an exported CSV file that is not UTF-8 | compile | none | ✓ line 2 of 'Rental_Owner_Statement.csv' is not UTF-8 text (byte 0x96) / hint: the file was probably saved as Windows-1252 or Latin-1; save it as UTF-8 (in Excel: Save As > CSV UTF-8) |
| S16 | [neuniversity/ALY6140#33](https://github.com/neuniversity/ALY6140/issues/33) | a tab-separated text file in Latin-1 | compile | none | ✓ line 2 of 'MisleInjury.txt' is not UTF-8 text (byte 0xED) / hint: the file was probably saved as Windows-1252 or Latin-1; save it as UTF-8 (in Excel: Save As > CSV UTF-8) |
| S17 | [reiinakano/scikit-plot#94](https://github.com/reiinakano/scikit-plot/issues/94) | a predictions CSV has an extra index column (8 columns for 7 classes) | none | none | no suitable contract: prediction columns against classes |
| S17 | [cbasemaster/lstmcorona#1](https://github.com/cbasemaster/lstmcorona/issues/1) | the code hard-codes a dataset size of 100; the file has 167 rows | none | none | model code: a hard-coded dataset size |
| S17 | [aaassasdf/Flower_Classification#1](https://github.com/aaassasdf/Flower_Classification/issues/1) | 603 images in the folder, 210 rows in the label CSV | contract | contract | ✓ contract violated: match rate of flowers is 66.67%, expected at least 100% / note: images: 6, labels: 4, matched: 4 / note: images without a label: 2 (0005.png, 0006.png) / hint: fix the file names in labels.file or the image files |

## Signatures

- S1: cannot write mode RGBA as JPEG (Pillow)
- S2: cannot identify image file (Pillow)
- S3: !ssize.empty() in function 'resize' (OpenCV: the image was not loaded)
- S4: Unsupported depth image for selected encoder is fallbacked to CV_8U (OpenCV)
- S5: expected input ... to have N channels, but got M channels instead (PyTorch)
- S6: stack expects each tensor to be equal size (PyTorch)
- S7: No labels found (YOLO data loaders)
- S8: ... not in index (pandas KeyError)
- S9: could not convert string to float
- S10: '<' not supported between instances of 'str' and 'int'
- S11: Input contains NaN (scikit-learn)
- S12: The feature names should match those that were passed during fit (scikit-learn)
- S13: cannot convert float NaN to integer
- S14: Index contains duplicate entries, cannot reshape (pandas pivot)
- S15: time data ... does not match format
- S16: UnicodeDecodeError when reading a CSV file
- S17: Found input variables with inconsistent numbers of samples (scikit-learn)

## Read but not qualifying

- S1 Zheng-Chong/CatVTON#140: in-memory image from a web UI, not a data file
- S2 tmbb/image-occlusion-2#2: UI state, no data-file cause
- S2 fchollet/deep-learning-with-python-notebooks#162: no root cause given
- S3 ageitgey/face_recognition#927: camera stream
- S3 nwesem/mtcnn_facenet_cpp_tensorRT#50: empty camera frame
- S3 santarl/Machi-Archived#1: no root cause given
- S3 JaidedAI/EasyOCR#1437: empty crop inside the OCR model
- S3 PaddlePaddle/PaddleX#4762: empty crop inside the OCR model
- S4 guofei9987/blind_watermark#167: root cause not established
- S4 giddyyupp/VLMDiff#2: question about training loss
- S4 metadriverse/metadrive#873: rendering problem
- S4 FantasticGNU/UniVAD#20: model code error
- S4 PAULYZHENG/UniHair#4: installation
- S4 OPPOMKLab/u-LLaVA#6: model inference output
- S4 PAULYZHENG/UniHair#6: model download
- S4 opencv/opencv#29365: feature request
- S4 geopavlakos/hamer#103: GPU memory
- S4 CalcuLuUus/RASM#11: hyperparameters
- S4 flame-cai/win64-local-ocr-tool#6: segmentation quality
- S5 zhengdaoli/AGC-net#3: no root cause given
- S6 open-edge-platform/geti-instant-learn#1125: empty mask inside the model
- S6 IceClear/CLIP-IQA#30: no root cause given
- S6 kohya-ss/sd-scripts#522: root cause not established
- S7 mf1024/ImageNet-Datasets-Downloader#27: question, not a failure
- S7 microsoft/banknote-net#12: model output labels
- S8 idekerlab/pyNBS#7: no root cause given
- S8 Stonesjtu/pytorch_memlab#53: profiler, not data
- S8 Seedarchangel/TuChart#19: data from a web API
- S8 SunPengChuan/wgdi#59: no root cause given
- S8 GeneralMills/pytrends#316: data from a web API
- S9 Conexo-Casa/govee-thermometer-ha#1: device reading from an API
- S9 jonasbkarlsson/ev_smart_charging#328: home-automation sensor state
- S9 FireShard/FYP#3: web form input
- S9 solidfox/sigenergy-cloud#6: cloud API response
- S9 mtreinish/stestr#398: test-result stream, not a dataset
- S9 voxel51/fiftyone#5498: library version mismatch
- S9 FXTD-ODYSSEY/renderdoc2fbx#19: empty report
- S10 cesarmaciel1234/cobrofacil-releases: point-of-sale app crash reports
- S10 dev-ir/xui-assistant#23: no details
- S10 ansible-collections/community.general#4451: version strings, not data
- S10 explosion/spacy-llm#411: configuration and Python version
- S10 LibreTranslate/argos-translate-files#20: PDF translation, not a dataset
- S10 Bilkent-CYBORG/ACC-UCB#1: plotting, no root cause given
- S10 mozilla-releng/balrog#3441: version string from an app
- S10 pwin/owlready2#34: ontology library
- S10 huangyz0918/termax#55: API client
- S10 lingualab/Text2Variable#1: root cause not established
- S11 Adrien-df/reconciliation_hts#1: no root cause given
- S11 XZhang97666/MultimodalMIMIC#7: NaN from training
- S11 zyang1580/CoLLM#6: NaN from training
- S11 pmcdi/jarvais-api-service#3: no root cause given
- S11 mindspore-ai/mindspore#383: library function
- S11 OUC-VAS/ForensicsAdapter#10: NaN from training
- S11 mims-harvard/Raindrop#5: NaN from training
- S11 caoyunkang/AdaCLIP#36: NaN from training
- S11 shenweichen/GraphEmbedding#33: NaN from training
- S11 parthsarthi03/raptor#3: NaN from an embedding model
- S11 naszilla/tabzilla#82: NaN model predictions
- S11 rparsa49/NYPC-DCT#2: hard-coded configuration
- S12 sktime/pycaret#69: bug inside the library
- S13 scverse/scanpy#1259: root cause not established
- S13 open-edge-platform/physical-ai-studio#452: NaN model output
- S13 mwaskom/seaborn#3963: plotting
- S13 Comfy-Org/ComfyUI#13149: model node
- S13 kijai/ComfyUI-WanVideoWrapper#1190: model node
- S13 Cyrax321/SNAGLINE#349: run-time metrics, not data
- S13 broadinstitute/StrainGE#31: root cause not established
- S13 egyptdj/stagin#15: analysis output
- S13 iprada/Circle-Map#79: root cause not established
- S13 DiamondLightSource/python-icebreaker#10: root cause not established
- S15 YelpArchive/elastalert#2908: log documents from a database
- S15 celebi-pkg/flight-analysis#29: scraped web data
- S15 weilbith/not-much-fire#1: mail timestamps
- S15 DeepL/deepl-python#125: API response
- S15 Ramya-Org-sample/Django-test-project#39: web form input
- S15 Ramya-Org-sample/Django-test-project#19: web form input
- S15 NinaWie/dav_hut_finder#42: web request
- S15 probberechts/soccerdata#909: scraped data, locale
- S15 Kaggle/kaggle-cli#523: API response
- S16 pandas-dev/pandas#55197: feature request
- S16 topprismdata/mousheng-outlet-insight#1: test harness
- S16 rinoshiyo/apple-health-mcp-server#233: database error strings
- S17 Necrosource/scikit-multilearn#35: indexing mistake in model code
- S17 Why-NUAA/paper#2: inference code
- S17 amazon-science/patchcore-inspection#46: root cause not established
- S17 Necrosource/scikit-multilearn#36: root cause not established
- S17 scikit-multilearn/scikit-multilearn#184: same report as Necrosource/scikit-multilearn#35
- S17 scikit-multilearn/scikit-multilearn#187: same report as Necrosource/scikit-multilearn#36
- S17 ivineetm007/Fall-detection#8: root cause not established
- S17 gitshanks/fer2013#5: no details
- S17 hyperopt/hyperopt-sklearn#150: bug inside the library
