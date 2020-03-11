# Esk8.Board.Controller

Smooths a value by using an array that stores values that you `add`.

You can specify:
- the length of the array (`factor`)
- how many values in the array before you start adding your new values (`seed`), the other values default to values that are ignored when averaged to `get` the smoothed value

The length of the array (`factor`) determines how smoothed the value is.

A common pattern would be to be entering in values and if you want to change something in a hurry, you can `clear` the array and re-seed it in time for new values (I did this for an electric skateboard remote).

Credit goes to [Smooth](https://github.com/MattFryer/Smoothed) repo that I 'enhanced' for my purpose (the pre-seeding).
