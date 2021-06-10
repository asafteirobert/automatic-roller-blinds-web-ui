rem Remove previous folder
rmdir "..\data" /s /q
mkdir "..\data\www"
rem Copy favicon related files
xcopy "..\www\android-chrome-192x192.png" "..\data\www\"
xcopy "..\www\android-chrome-256x256.png" "..\data\www\"
xcopy "..\www\apple-touch-icon.png" "..\data\www\"
xcopy "..\www\browserconfig.xml" "..\data\www\"
xcopy "..\www\favicon.ico" "..\data\www\"
xcopy "..\www\favicon-16x16.png" "..\data\www\"
xcopy "..\www\favicon-32x32.png" "..\data\www\"
xcopy "..\www\mstile-150x150.png" "..\data\www\"
xcopy "..\www\safari-pinned-tab.svg" "..\data\www\"
xcopy "..\www\site.webmanifest" "..\data\www\"
rem Copy fonts(font awesome icons)
xcopy "..\www\fonts\*" "..\data\www\fonts\"

rem Inline all marked css and js files into one big html, using https://github.com/popeindustries/inline-source-cli
rem This requires Node.js to be installed. npx will install inline-source-cli automatically
call npx --package inline-source-cli inline-source --compress true --root ../www/ ../www/index.html ../www/index.html.all

rem Move the inlined html
copy "..\www\index.html.all" "..\data\www\index.html"
del "..\www\index.html.all"

rem gzip the big index file to reduce the size significantly
gzip.exe -f "../data/www/index.html"

rem Wait for input
pause
