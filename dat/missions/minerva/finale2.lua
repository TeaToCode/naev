--[[
<?xml version='1.0' encoding='utf8'?>
<mission name="Minerva Finale 2">
 <unique />
 <priority>1</priority>
 <chance>100</chance>
 <location>Bar</location>
 <spob>Regensburg</spob>
 <done>Minerva Finale 2</done>
 <notes>
  <campaign>Minerva</campaign>
 </notes>
</mission>
--]]
--[[
   Final mission having to revisit and break into Minerva Station
--]]
local minerva = require "common.minerva"
local vn = require 'vn'
--local vni = require 'vnimage'
--local vne = require "vnextras"
local fmt = require "format"
--local pilotai = require "pilotai"
--local love_audio = require 'love.audio'
--local reverb_preset = require 'reverb_preset'
local ccomm = require "common.comm"
--local lmisn = require "lmisn"
--local der = require 'common.derelict'
--local tut = require 'common.tutorial'

local mainspb, mainsys = spob.getS("Minerva Station")
local returnspb, returnsys = spob.getS("Shangris Station")
local title = _("Minerva Station Redux")

-- Mission states:
--  nil: mission not accepted yet
--    1. Get to Minerva Station
--    2. Hack the gibson
--    3. Won fight
--    4. On way to darkshed
mem.state = nil

function create ()
   misn.finish(false)
   if not misn.claim( mainspb ) then
      misn.finish( false )
   end
   misn.setNPC( minerva.maikki.name, minerva.maikki.portrait, _("Maikki seems to be waiting for you in her regular clothes.") )
   misn.setDesc(fmt.f(_([[Maikki has entrusted you with raiding the weapons laboratory at Minerva Station to obtain anything that can help Kex, while also plundering anything you find of value. Afterwards she has told you to meet her up at {returnspb} in the {returnsys} system.]]),
      {returnspb=returnspb, returnsys=returnsys}))
   misn.setReward(_("Saving Kex!"))
   misn.setTitle( title )
end

local talked -- Reset when loads
function accept ()
   vn.clear()
   vn.scene()
   local maikki =vn.newCharacter( minerva.vn_maikki() )
   vn.music( minerva.loops.maikki )
   vn.transition()

   if not talked then
      talked = true

      vn.na(_([[You join Maikki at her table. Although she has changed into her civilian clothes, her composure and facial expression is quite unlike when you first met her.]]))
      maikki(_([["I've talked with the pirate head surgeon, and there's both good news and bad news."]]))
      vn.menu{
         {[["Good news?"]], "01_good"},
         {[["Bad news?"]], "01_bad"},
      }

      -- Just change the order she says stuff in
      local vn01good, vn01bad
      vn.label("01_good")
      maikki(_([["The good news is that Zuri is somewhat stable. It's still not clear if she's going to pull it off, but we have to trust her fortitude. We'll have to fly her back to get some more proper follow-up care. However, at least the worst has been avoided for now."]]))
      vn.func( function ()
         vn01good = true
         if not vn01bad then
            vn.jump("01_bad")
         else
            vn.jump("01_cont")
         end
      end )

      vn.label("01_bad")
      maikki(_([["The bad news is that we don't know what the deal is with my father. He seems to be alive, however, the surgeon had no idea what to do. I guess this is a first for them. We got a second look by the lead engineer who told us that it seems like some of his circuitry was damaged."]]))
      vn.func( function ()
         vn01bad = true
         if not vn01good then
            vn.jump("01_good")
         else
            vn.jump("01_cont")
         end
      end )

      vn.label("01_cont")
      maikki(_([["Damn it, I should have brought my squad with me, but I erred on the side of cautious. How unlike me! But with Zuri and Kex's state, we're going to have to take them to a proper medical facility. That doesn't mean we can leave Minerva Station as it is, after all we've been through!"]]))
      local winner = var.peek("minerva_judgement_winner")
      local msg
      if winner=="dvaered" then
         msg = _([["Minerva Station is going to be swarming with Dvaereds brutes in no time when it gets handed over. ]])
      elseif winner=="zalek" then
         msg = _([["Minerva Station is going to be swarming with Za'leks and their pesky drones in no time when it gets handed over. ]])
      else -- "independent"
         msg = _([["We don't know what's going to happen to the station after the trial ended as it did. I wouldn't be surprised if it would be flooded with Imperials in a short time. ]])
      end
      maikki(msg.._([[We have to infiltrate the weapons facility at the station and not only plunder some nice weapon prototypes and blueprints, but now we also have to see if we can find anything that can help my father! It's a bit of a long shot, but it's our best bet."]]))
      vn.menu( {
         {_([["Wait, you knew about the station?"]]), "02_plan"},
         {_([["Anything for Kex!"]]), "02_kex"},
      } )

      vn.label("02_plan")
      maikki(_([["A pirate has to be efficient: finding my father, and infiltrating a weapons facility. Two birds with one stone! I wasn't entirely sure, but I had a good lead on the fact that the Minerva CEO was doing quite a bit more than gambling."]]))
      vn.jump("02_cont")

      vn.label("02_kex")
      maikki(_([["Yes. You have no idea how much shit I've had to put up with to find him. There is no way I'm going to lose him now! The rest can wait!"]]))
      vn.jump("02_cont")

      vn.label("02_cont")
      maikki(_([["Although I'm really tempted to storm Minerva Station myself, and plunder the weapons facility like any pirate dreams of, but it pisses me off that I'm going to have to leave it to you. Damn responsibilities."]]))
      maikki(_([["Zuri got most of the information and should be the one to brief you on the weapons facility, but that's not going to work out now."
She lets out a sigh.
"I'll try to give you a short rundown on what we know."]]))
      maikki(_([[She clears her throat.
"So, our intel hints that they are working on an experimental energy weapon of some time at the station. Should be quite preliminary design, but we don't have much info on the current state of development. Either way, it's going to be useful and/or worth a fortune!"]]))
      maikki(_([["Apparently, the laboratory is located in a penthouse to not raise suspicion. Security is quite tight around the area, but we've got the likely location narrowed down. I forget where it was, but I'll send you the documents we have on it."]]))
      maikki(_([["We don't really know what we'll find there, but I guess you'll have to improvise? It's pretty much now or never, and we won't get a second chance once the station gets locked down."]]))
      maikki(_([["You'll have to make it to the station on your own, but once you get there, some pirate operatives should be there to help you ransack the place. You won't have too much time, so just try to grab whatever you can and get out of there. Make sure to keep an eye out for any sort of thing that can help Kex. I'm not sure our engineers will be able to figure it out by themselves."]]))
      maikki(_([[She gives a solemn nod.
"Are you ready to infiltrate Minerva Station one last time?"]]))
   else
      maikki(_([["Are you ready now to infiltrate Minerva Station?"]]))
   end

   vn.menu( {
      {_("Accept the Undertaking"), "accept"},
      {_("Prepare more"), "decline"},
   } )

   vn.label("decline")
   vn.na(_([[Maikki gives you some time to prepare more. Return to her when you are ready.]]))
   vn.done()

   vn.label("accept")
   vn.func( function () mem.state=1 end )
   maikki(_([["Great! I knew I could count on you."]]))
   maikki(fmt.f(_([["I want to take Kex and Zuri to {spb} in the {sys} system, where I know a gal that should help patch them up. Not as good as the surgeons back at New Haven, but it'll have to do. We don't have the time to make the full trip at the moment, once you join us, we can figure out if we can make the trip."]]),
      {spb=returnspb, sys=returnsys}))
   maikki(fmt.f(_([["You infiltrate the station, get what you can, and meet up with us at {spb}. We'll then figure out what to do. Best of luck!"
Maikki gives you a weird two finger salute and takes off to her ship, leaving you to do your part on your own.]]),
      {spb=returnspb}))

   vn.run()

   -- If not accepted, mem.state will still be nil
   if mem.state==nil then
      return
   end

   misn.accept()
   misn.osdCreate( title, {
      fmt.f(_("Land on {spb} ({sys} system)"),{spb=mainspb, sys=mainsys}),
      _("Ransack the weapon laboratory"),
      fmt.f(_("Meet up with Maikki at {spb} ({sys} system)"),{spb=returnspb, sys=returnsys}),
   } )
   mem.mrk = misn.markerAdd( mainspb )

   hook.enter("enter")
end

local boss, guards, hailhook, bosshailed
function enter ()
   local scur = system.cur()
   if scur==mainsys and mem.state==1 and not mem.boss_died then
      pilot.clear()
      pilot.toggleSpawn(false)

      local pos = mainspb:pos() + vec2.new( 100+200*rnd.rnd(), rnd.angle() )
      boss = pilot.add( "Empire Peacemaker", "Empire", pos, nil, {ai="guard"} )
      guards = { boss }
      for k,v in ipairs{"Empire Lancelot", "Empire Lancelot", "Empire Lancelot", "Empire Lancelot"} do
         local p = pilot.add( v, "Empire", pos+rnd.rnd(50,rnd.angle()), nil, {ai="guard"} )
         p:setLeader( boss )
         table.insert( guards, p )
      end

      bosshailed = false
      mainspb:landDeny(true,_("Special authorization needed."))

      hailhook = hook.hail_spob( "comm_minerva" )
      hook.pilot( boss, "hail", "comm_boss" )
      hook.pilot( boss, "death", "boss_death" )
   elseif scur==mainsys and mem.state==2 then
      -- Set up fight
      pilot.clear()
      pilot.toggleSpawn(false)
   else
      -- Clean up some stuff if applicable
      if hailhook then
         hook.rm( hailhook )
         hailhook = nil
      end
   end
end

local landack, timetodie
local askwhy, left01, left02, left03, triedclearance, inperson
local function talk_boss( fromspob )
   if timetodie then
      player.msg(_("Communication channel is closed."))
      return
   elseif landack then
      if boss and boss:exists() then
         boss:comm(_("Proceed to land."))
      end
      return
   elseif inperson then
      if boss and boss:exists() then
         boss:comm(_("Please bring the documents in person."))
      end
      return
   end

   vn.clear()
   vn.scene()
   local p = ccomm.newCharacter( vn, boss )
   vn.transition()
   if fromspob then
      vn.na(fmt.f(_([[You try to open a communication channel with {spb}, but get rerouted to an Imperial ship.]]),
         {spb=mainspb}))
   end
   if bosshailed then
      p(_([["What do you want again? I told you Minerva Station is locked down until further notice."]]))
   else
      p(_([["What do you want? Minerva Station is locked down until further notice."]]))
      vn.func( function () bosshailed = true end )
   end

   vn.label("menu")
   vn.menu( function ()
      local opts = {
         {_([["Why is it locked down?"]]), "01_why"},
         {_([[Leave.]]), "leave"},
      }
      table.insert( opts, 2, {_([["I'm here to do cleaning."]]), "01_contractor"} )
      if left03 then
         table.insert( opts, 2, {_([["Please, let me land!"]]), "01_left4"} )
      elseif left02 then
         table.insert( opts, 2, {_([["It's a matter of life and death!"]]), "01_left3"} )
      elseif left01 then
         table.insert( opts, 2, {_([["What I left is really important!"]]), "01_left2"} )
      else
         table.insert( opts, 2, {_([["I left something at the station."]]), "01_left"} )
      end
      if askwhy and not triedclearance then
         table.insert( opts, 2, {_([["I brought Imperial clearance."]]), "01_clearance"} )
      end
      return opts
   end )

   vn.label("01_why")
   p(_([["Imperial Decree 28701-289 is why. No access without proper Imperial clearance. No exceptions."]]))
   vn.func( function () askwhy = true end )
   vn.jump("menu")

   vn.label("01_left")
   p(_([["Tough luck buddy. Judge's orders. Not going to risk a pay cut for someone I don't know. File an EB-89212-9 with information on it and you may be able to get it back."]]))
   vn.func( function () left01 = true end )
   vn.jump("menu")

   vn.label("01_left2")
   p(_([["Everyone thinks everything is important to them, buddy. The richest man is he who needs nothing, but just go file an EB-89212-9 if you want it back."]]))
   vn.func( function () left02 = true end )
   vn.jump("menu")

   vn.label("01_left3")
   p(_([["Why, you should have have said so at the beginning! No, seriously, buddy. File the EB-89312-9. What, or was it EB-89213-9? Anyway, you're not getting through. Don't push your luck."]]))
   vn.func( function () left03 = true end )
   vn.jump("menu")

   vn.label("01_left4")
   p(_([["Would have preferred to have an office job instead of having to deal with punks..."
They let out a sigh.]]))
   vn.jump("timetodie")

   vn.label("01_contractor")
   vn.func( function ()
      local s = player.pilot():ship()
      local t = s:tags()
      if t.standard and t.transport then
         vn.jump("contractor_ok")
      else
         vn.jump("contractor_bad")
      end
   end )
   vn.label("contractor_ok")
   p(_([["Great! We were expecting you, it's a mess down there. Proceed to land."]]))
   vn.jump("landack")
   vn.label("contractor_bad")
   p(fmt.f(_([["Wait, why are you in a {ship}? Cleaning crew usually comes in a Koala or Mule? Show me your credentials!"]]),
      {ship=player.pilot():ship()}))
   vn.menu{
      {_([["Company is doing a fleet renewal."]]),"contractor_bad_renewal"},
      {_([["Left it at the office."]]),"contractor_bad_left"}
   }
   vn.label("contractor_bad_renewal")
   vn.func( function ()
      local pp = player.pilot()
      local weaps = pp:outfitsList("weapon")
      if #weaps >= 2 then
         vn.jump("contractor_bad_renewal_fight")
      else
         vn.jump("contractor_bad_left")
      end
   end )
   vn.label("contractor_bad_renewal_fight")
   p(_([["What's the deal with all the weapons you're sporting then? You look like you're looking for trouble, but the Empire always takes out the trash!"]]))
   vn.jump("timetodie")
   vn.label("contractor_bad_left")
   p(_([["Go get them then. You are not getting landing access without your credentials."]]))
   vn.na(_([[The communication channel cuts off.]]))
   vn.done()

   vn.label("01_clearance")
   vn.func( function () triedclearance = true end )
   p(_([["OK, please send the clearance codes."]]))
   vn.menu{
      {_([[Send a meme.]]),"clearence_meme"},
      {_([[Send random data.]]),"clearence_random"},
   }
   vn.label("clearence_meme")
   p(_([[You hear a chuckle before they clear their throat.
"You do know that impersonation is an Imperial felony, right?"]]))
   vn.menu{
      {_([["That chuckle means I can land right?"]]),"clearance_meme_die"},
      {_([[Apologize.]]),"clearance_meme_apologize"},
   }
   vn.label("clearance_meme_die")
   p(_([["You brought this upon yourself!"]]))
   vn.jump("timetodie")
   vn.label("clearance_meme_apologize")
   vn.na(_([[You apologize profusely.]]))
   p(_([["Make sure it doesn't happen again. I'll be writing a report on this, next time, expect no leniency."]]))
   vn.na(_([[The communication channel closes.]]))
   vn.done()
   vn.label("clearance_random")
   vn.na(fmt.f(_([[You stream a large block of random data to {boss}.]]),
      {boss=boss}))
   p(_([["I'm sorry, but it seems like the communication channel is corrupting. Please bring the documents in person."]]))
   vn.func( function ()
      inperson = true
      boss:setActiveBoard(true)
      hook.pilot( boss, "board", "board_boss" )
   end )
   vn.done()

   vn.label("timetodie")
   vn.func( function () timetodie = true end )
   vn.na(_([[The communication channel cuts off as your sensors pick up signals of weapons powering up.]]))
   vn.done()

   vn.label("landack")
   vn.na(_([[Your navigation system lights up green as you receive a confirmation of landing access to Minerva Station.]]))
   vn.func( function () landack=true end )
   vn.done()

   vn.menu("leave")
   vn.na(_([[You close the communication channel.]]))
   vn.done()

   if landack then
      mainspb:landAllow(true)
   elseif timetodie then
      for k,v in ipairs(guards) do
         v:setHostile(true)
      end
      if hailhook then
         hook.rm( hailhook )
         hailhook = nil
      end
   end

   vn.run()
end

function comm_boss()
   talk_boss( false )
   player.commClose()
end

function comm_minerva( commspb )
   if commspb ~= mainspb then return end
   talk_boss( true )
   player.commClose()
end

function boss_death ()
   mem.boss_died = true
   if hailhook then
      hook.rm( hailhook )
      hailhook = nil
   end

   mainspb:landAllow(true)
   player.msg(fmt.f(_("With the blocking ship out of the way, you should be able to land on {spb} now."), {spb=mainspb}))
end

function board_boss ()
   vn.clear()
   vn.scene()
   vn.transition()
   vn.na(fmt.f(_([[You approach the {plt} to dock. The {plt} has their shields down for boarding, it would be a good time to try to make use uf the situation.]]),
      {plt=boss}))
   vn.menu{
      {_([[Board normally.]]),"01_board"},
      {fmt.f(_([[Open fire on the {plt} at point blank.]]),{plt=boss}),"01_fire"},
      {fmt.f(_([[Ram the {plt}.]]),{plt=boss}),"01_ram"},
   }

   vn.label("01_fire")
   vn.func( function ()
      boss:setHealth( 70, 0 )
      boss:setEnergy( 0 )
      for k,v in ipairs(guards) do
         v:setHostile(true)
      end
      mainspb:landAllow(false) -- Clear landing status
   end )
   vn.na(fmt.f(_([[You quickly power up your weapons and aim at critical ship infrastructure right as the boarding clamps begin to extend. Catching the {plt} off guard, your weapons are able to do significant damage, even knocking several systems offline. However, you have no choice now but to finish the job. Time to power on your weapons.]]),
      {plt=boss}))
   vn.done()

   vn.label("01_ram")
   vn.func( function ()
      if player.pilot():mass() > 0.4 * boss:mass() then
         vn.jump("ram_good")
      else
         vn.jump("ram_bad")
      end
   end )
   vn.label("ram_good")
   vn.func( function ()
      boss:setHealth( 40, 0 )
      boss:setEnergy( 0 )
      boss:disable(true) -- not permanent
      for k,v in ipairs(guards) do
         v:setHostile(true)
      end
      mainspb:landAllow(false) -- Clear landing status
   end )
   vn.na(fmt.f(_([[Right before boarding, you power on your shields and slam on your ship thrusters. Catching the {plt} completely off guard with your maneuver, your ship smashes into their hull causing massive damage. Time to power on your weapons.]]),
      {plt=boss}))
   vn.done()
   vn.label("ram_bad")
   vn.func( function ()
      local ppm = player.pilot():mass()
      local bm = boss:mass()
      boss:setHealth( (bm-ppm)/bm, 0 )
      boss:setEnergy( 0 )
      for k,v in ipairs(guards) do
         v:setHostile(true)
      end
      mainspb:landAllow(false) -- Clear landing status
   end )
   vn.na(fmt.f(_([[Right before boarding, you power on your shields and slam on your ship thrusters. Catching the {plt} completely off guard with your maneuver, your ship smashes into their hull causing significant damage. Time to power on your weapons.]]),
      {plt=boss}))
   vn.done()

   vn.label("01_board")
   vn.na(_([[You board the ship and are escorted to the command room where the captain is waiting. Getting this far and not having any other choice, you give them a holodrive with some randomly generated contents. They plug in the drive and try to authorize it, however, an error appears as expected.]]))
   vn.na(_([[The first officer attempts percussive maintenance on the main system, however, as expected, the error does not go away. The captain is unfazed by the situation, and, muttering something about the new Imperial Operating System update, says something about verifying it later on your way out. Surprisingly enough, you are given permission to land.]]))
   vn.na(_([[You quickly make your way back to your ship to prepare to land before they see through your bullshit.]]))
   vn.func( function ()
      landack=true
      mainspb:landAllow(true)
   end )
   vn.done()

   vn.run()

   player.unboard()
end
