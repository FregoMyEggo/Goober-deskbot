
# Goober-deskbot
**Tamagotchi inspired desk companion/clock. Feed it, play with it, watch it evolve. Powered by esp32**

Requires Arduino IDE with these libraries: - ESP32 Boards package by Espressif Systems and LVGL library

Step 1:
Download the entire folder titled "assets," unzip it, and copy it directly to your micro SD card.The main folder
must be titled 'assets' when copied to the sd card (the images inside are all .raw, which esp32 can read, but thumbnails
won't show on your computer unless you have an image viewer capable of decoding raw images).

Step 2:
Download or extract the Goober folder anywhere you want.
Open Arduino IDE, navigate to the Goober folder, and open:
Gooberv4_sketchbook.ino

Step 3:
Plug in your Waveshare ESP32-S3 1.46" display device and flash the sketch.
Done.


---------------If you want to make changes--------------------------

The only sketch you should modify is Gooberv4_sketchbook.ino. All others are just the default support files that came from waveshare, changing those may break things.

1) Change the word "dude" to any name you want for personalized greetings (cntrl+f to find "dude").


2) Create your own characters (long, tedious process). You can change image files in the file structure (on the sd card) to create new characters. 5 images simply loop to create the sprite animation. All images must be 256x256 and formatted to .raw.

character progression is:
character1a->character1b->character1c
character2a->character2b->character2c
.
.
character6a->character6b->character6c


One tricky detail. Each character's "feed" folder is not very intuitive. Here is how those frames are called for the feed screen:
1.raw ---displays as static image
2.raw & 3.raw-- this is 2 frames of the character rejecting a food item (usually shakes head left, then right)
4.raw & 5.raw --- this is 2 frames of the character accepting food item (usually opens mouth, then chomps down)

3) For testing purposes, you can speed up game time by changing static uint32_t GOOBER_YEAR_MS = 76400000UL.  Something much faster ("900" will age Goober 1 year every .9 seconds.).  "86400000" ages Goober 1 year for every 1 day, but by default it is set to 76400000UL.

Disclaimer: This project may include AI-generated fan-inspired artwork. It is an unofficial hobby project and is not affiliated with, endorsed by, or intended to depict any official copyrighted character exactly as-is.  

All code is AI slop.. sorry about that.

<img width="3024" height="4032" alt="IMG_6328" src="https://github.com/user-attachments/assets/8422a187-987f-4d9a-a021-db2002fbcfea" />
<img width="3024" height="4032" alt="IMG_6327" src="https://github.com/user-attachments/assets/2905e2a1-a4ca-462d-8919-578b31220476" />

