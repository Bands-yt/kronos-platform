-- Behaviour checks for gui_menu.rbxlx.
local gui = player.PlayerGui:WaitForChild("Menu", 2)
check("the menu is copied to the player", gui ~= nil)
local box = gui.Codes.CodeBox
check("the shop's canvas fits its items", gui.Shop.CanvasSize == UDim2.new(0, 0, 0, 330), tostring(gui.Shop.CanvasSize))
check("the code box shows its placeholder", box.PlaceholderText == "Enter a code")
box:CaptureFocus()
box.Text = "kronos"
box:ReleaseFocus(true)
task.wait(0.2)
check("typing a code and pressing Enter redeems it", player:GetAttribute("Gems") == 100, tostring(player:GetAttribute("Gems")))
check("the box is cleared afterwards", box.Text == "")
box:CaptureFocus()
box.Text = "KRONOS"
box:ReleaseFocus(true)
task.wait(0.2)
check("a code only works once", player:GetAttribute("Gems") == 100, tostring(player:GetAttribute("Gems")))
box.Text = "launch"
click(gui.Codes.Redeem)
task.wait(0.2)
check("the Redeem button works too", player:GetAttribute("Gems") == 150, tostring(player:GetAttribute("Gems")))
local first = gui.Shop.Item1.AbsolutePosition
local second = gui.Shop.Item2.AbsolutePosition
check("the grid lays items out side by side", second.X > first.X and second.Y == first.Y, tostring(first) .. " " .. tostring(second))
