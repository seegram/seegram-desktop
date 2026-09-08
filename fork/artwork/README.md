# Application and tray artwork

`app-icon.svg` is the source for the default SeeGram application icon: a black
macOS-shaped background with the white eye. `Resources/art/disguise/eye.svg`
contains the same eye contours without a background for the system tray.
`Resources/art/disguise/telegram*.png` retain upstream Telegram artwork for
appearance selection and clean groups.

Run `node fork/generate-icons.cjs` from the repository with `sharp` available
in Node's module search path to regenerate the committed PNG and ICO assets.
The macOS asset catalog and iconset use the same generated artwork as the
runtime icon, so starting or quitting the client does not switch designs.
