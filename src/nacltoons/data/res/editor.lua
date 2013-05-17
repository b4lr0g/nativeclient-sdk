-- Copyright (c) 2013 The Chromium Authors. All rights reserved.
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.

--- Editor logic.

local util = require 'util'
local gui = require 'gui'
local drawing = require 'drawing'

local editor = {}

local MENU_DRAW_ORDER = 3
local VELOCITY_ITERATIONS = 8
local POS_ITERATIONS = 1

local actions = { ADD_SHAPE = 1, MOVE = 2 }
local undo_buffer = {}
local redo_buffer = {}

local function Set(list)
    local set = {}
    for _, l in ipairs(list) do set[l] = true end
    return set
end

local function SerializeLevel()
    local ignore_keys = Set({ 'tag', 'script', 'tag_map', 'tag_list', 'object_map' })
    local key_map = { tag_str = 'tag', script_name = 'script' }
    local output = util.TableToYaml(level_obj, ignore_keys, key_map)
    return '# Automatically generated by editor.lua\n\n' .. output
end

function editor.Update(delta)
    if level_obj.run_physics then
      level_obj.world:Step(delta, VELOCITY_ITERATIONS, POS_ITERATIONS)
    end
end

function editor.OnTouchBegan(x, y, tapcount)
    if drawing.IsDrawing() then
        return false
    end

    last_draw_time = CCTime:getTime()
    return drawing.OnTouchBegan(x, y, tapcount)
end

function editor.OnTouchMoved(x, y)
    drawing.OnTouchMoved(x, y)
end

local function AddAction(action_type, properties)
    properties.action = action_type
    redo_buffer = {}
    table.insert(undo_buffer, properties)
end

function editor.OnTouchEnded(x, y)
    last_drawn_shape = drawing.OnTouchEnded(x, y)
    AddAction(actions.ADD_SHAPE, { shape = last_drawn_shape })
end

local object_handlers = {}
local start_pos = nil
local touch_pos = nil

function object_handlers.OnTouchBegan(self, x, y, tapcount)
    -- Only move one object at a time.
    if touch_pos then
        return false
    end

    -- Store start local of object and start location of touch
    start_pos = ccp(self.node:getPositionX(), self.node:getPositionY())
    touch_pos = ccp(x, y)
    return true
end

function object_handlers.OnTouchMoved(self, x, y, tapcount)
    local delta = ccp(x - touch_pos.x, y - touch_pos.y)
    self.node:setPosition(ccp(start_pos.x + delta.x, start_pos.y + delta.y))
end

function object_handlers.OnTouchEnded(self)
    local new_position = ccp(self.node:getPositionX(), self.node:getPositionY())
    AddAction(actions.MOVE, { object = self,
                              old_position = start_pos,
                              new_position = new_position })
    touch_pos = nil
    start_pos = nil
end

local function HandleRestart()
    GameManager:sharedManager():Restart()
end

local function ChangeTool(value)
    if value == 'Line' then
        drawing.mode = drawing.MODE_LINE
    elseif value == 'Select' then
        drawing.mode = drawing.MODE_SELECT
    elseif value == 'Paint' then
        drawing.mode = drawing.MODE_FREEHAND
    elseif value == 'Circle' then
        drawing.mode = drawing.MODE_CIRCLE
    else
        error('unknown tool: ' .. value)
    end
end

local function Save(value)
   local string = SerializeLevel()
   print("***")
   print(string)
   print("***")
end

local function Undo(value)
    if #undo_buffer == 0 then
        return
    end
    item = table.remove(undo_buffer, #undo_buffer)
    table.insert(redo_buffer, item)
    if item.action == actions.MOVE then
        print("undo move")
        item.object.node:runAction(CCMoveTo:create(0.2, item.old_position))
    elseif item.action == actions.ADD_SHAPE then
        print("undo new")
        drawing.DestroySprite(item.shape.node)
        level_obj.object_map[1].node:setPosition(ccp(0, 0))
    else
        error('unknown undo action: ' .. item.action)
    end
end

local function Redo(value)
    if #redo_buffer == 0 then
        return
    end
    item = table.remove(redo_buffer, #redo_buffer)
    table.insert(undo_buffer, item)
    if item.action == actions.MOVE then
        print("redo move")
        item.object.node:runAction(CCMoveTo:create(0.2, item.new_position))
    elseif item.action == actions.ADD_SHAPE then
        print("redo new")
        -- drawing.DestroySprite(item.shape.node)
        -- level_obj.object_map[1].node:setPosition(ccp(0, 0))
    else
        error('unknown redo action: ' .. item.action)
    end
end

local function ToggleDebug()
    level_obj.layer:ToggleDebug()
end

local function ToggleRun()
    level_obj.run_physics = not level_obj.run_physics
end

local function HandleRestart()
    GameManager:sharedManager():Restart()
end

local function HandleExit()
    CCDirector:sharedDirector():popScene()
end

function editor.StartLevel(level_number)
    -- Create a textual menu it its own layer as a sibling of the LevelLayer
    level_obj.run_physics = false
    menu_def = {
        font_size = 24,
        align = 'Left',
        pos = { 10, 300 },
        items = {
            { name='Select', callback=ChangeTool },
            { name='Paint', callback=ChangeTool },
            { name='Circle', callback=ChangeTool },
            { name='Line', callback=ChangeTool },
            { name='Undo', callback=Undo },
            { name='Redo', callback=Redo },
            { name='Run', callback=ToggleRun },
            { name='Save', callback=Save },
            { name='Exit', callback=HandleExit },
            { name='Toggle Debug', callback=ToggleDebug },
        },
    }

    local menu = gui.CreateMenu(menu_def)
    local parent = level_obj.layer:getParent()
    parent:addChild(menu, MENU_DRAW_ORDER)

    -- Override all the object behaviour scripts.
    drawing.handlers = object_handlers
    for _, object in pairs(level_obj.object_map) do
        object.script = object_handlers
    end
end

return editor
