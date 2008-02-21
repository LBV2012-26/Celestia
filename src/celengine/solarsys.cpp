// solarsys.cpp
//
// Copyright (C) 2001-2006 Chris Laurel <claurel@shatters.net>
//
// Solar system catalog parser.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

#include <cassert>
// #include <limits>
#include <cstdio>

#ifndef _WIN32
#ifndef TARGET_OS_MAC
#include <config.h>
#endif /* ! TARGET_OS_MAC */
#endif /* ! _WIN32 */

#include <celutil/debug.h>
#include <celmath/mathlib.h>
#include <celutil/util.h>
#include <cstdio>
#include <limits>
#include "astro.h"
#include "parser.h"
#include "texmanager.h"
#include "meshmanager.h"
#include "universe.h"
#include "multitexture.h"
#include "parseobject.h"
#include "frametree.h"
#include "timeline.h"
#include "timelinephase.h"

using namespace std;


enum Disposition
{
    AddObject,
    ReplaceObject,
    ModifyObject,
};


/*!
  Solar system catalog (.ssc) files contain items of three different types:
  bodies, locations, and alternate surfaces.  Bodies planets, moons, asteroids,
  comets, and spacecraft.  Locations are points on the surfaces of bodies which
  may be labelled but aren't rendered.  Alternate surfaces are additional
  surface definitions for bodies.

  An ssc file contains zero or more definitions of this form:

  \code
  [disposition] [item type] "name" "parent name"
  {
     ...object info fields...
  }
  \endcode

  The disposition of the object determines what happens if an item with the
  same parent and same name already exists.  It may be one of the following:
  - Add - Default if none is specified.  Add the item even if one of the
    same name already exists.
  - Replace - Replace an existing item with the new one
  - Modify - Modify the existing item, changing the fields that appear
    in the new definition.

  All dispositions are equivalent to add if no item of the same name
  already exists.

  The item type is one of Body, Location, or AltSurface, defaulting to
  Body when no type is given.

  The name and parent name are both mandatory.
*/

static void errorMessagePrelude(const Tokenizer& tok)
{
    cerr << _("Error in .ssc file (line ") << tok.getLineNumber() << "): ";
}

static void sscError(const Tokenizer& tok,
                     const string& msg)
{
    errorMessagePrelude(tok);
    cerr << msg << '\n';
}


//! Maximum depth permitted for nested frames.
static unsigned int MaxFrameDepth = 50;

static bool isFrameCircular(const ReferenceFrame& frame, ReferenceFrame::FrameType frameType)
{
    return frame.nestingDepth(MaxFrameDepth, frameType) > MaxFrameDepth;
}



static Location* CreateLocation(Hash* locationData,
                                Body* body)
{
    Location* location = new Location();

    Vec3d longlat(0.0, 0.0, 0.0);
    locationData->getVector("LongLat", longlat);

    Vec3d position = body->planetocentricToCartesian(longlat.x,
                                                     longlat.y,
                                                     longlat.z);
    location->setPosition(Vec3f((float) position.x, (float) position.y, (float) position.z));

    double size = 1.0;
    locationData->getNumber("Size", size);
    location->setSize((float) size);

    double importance = -1.0;
    locationData->getNumber("Importance", importance);
    location->setImportance((float) importance);

    string featureTypeName;
    if (locationData->getString("Type", featureTypeName))
        location->setFeatureType(Location::parseFeatureType(featureTypeName));

    return location;
}


static void FillinSurface(Hash* surfaceData,
                          Surface* surface,
                          const std::string& path)
{
    surfaceData->getColor("Color", surface->color);

    // Haze is deprecated; used only in pre-OpenGL 2.0 render paths
    Color hazeColor = surface->hazeColor;
    float hazeDensity = hazeColor.alpha();
    if (surfaceData->getColor("HazeColor", hazeColor) | surfaceData->getNumber("HazeDensity", hazeDensity))
    {
        surface->hazeColor = Color(hazeColor.red(), hazeColor.green(),
                                   hazeColor.blue(), hazeDensity);
    }

    surfaceData->getColor("SpecularColor", surface->specularColor);
    surfaceData->getNumber("SpecularPower", surface->specularPower);

    surfaceData->getNumber("LunarLambert", surface->lunarLambert);

    string baseTexture;
    string bumpTexture;
    string nightTexture;
    string specularTexture;
    string normalTexture;
    string overlayTexture;
    bool applyBaseTexture = surfaceData->getString("Texture", baseTexture);
    bool applyBumpMap = surfaceData->getString("BumpMap", bumpTexture);
    bool applyNightMap = surfaceData->getString("NightTexture", nightTexture);
    bool separateSpecular = surfaceData->getString("SpecularTexture",
                                                   specularTexture);
    bool applyNormalMap = surfaceData->getString("NormalMap", normalTexture);
    bool applyOverlay = surfaceData->getString("OverlayTexture",
                                               overlayTexture);

    unsigned int baseFlags = TextureInfo::WrapTexture | TextureInfo::AllowSplitting;
    unsigned int bumpFlags = TextureInfo::WrapTexture | TextureInfo::AllowSplitting;
    unsigned int nightFlags = TextureInfo::WrapTexture | TextureInfo::AllowSplitting;
    unsigned int specularFlags = TextureInfo::WrapTexture | TextureInfo::AllowSplitting;

    float bumpHeight = 2.5f;
    surfaceData->getNumber("BumpHeight", bumpHeight);

    bool blendTexture = false;
    surfaceData->getBoolean("BlendTexture", blendTexture);

    bool emissive = false;
    surfaceData->getBoolean("Emissive", emissive);

    bool compressTexture = false;
    surfaceData->getBoolean("CompressTexture", compressTexture);
    if (compressTexture)
        baseFlags |= TextureInfo::CompressTexture;

    if (blendTexture)
        surface->appearanceFlags |= Surface::BlendTexture;
    if (emissive)
        surface->appearanceFlags |= Surface::Emissive;
    if (applyBaseTexture)
        surface->appearanceFlags |= Surface::ApplyBaseTexture;
    if (applyBumpMap || applyNormalMap)
        surface->appearanceFlags |= Surface::ApplyBumpMap;
    if (applyNightMap)
        surface->appearanceFlags |= Surface::ApplyNightMap;
    if (separateSpecular)
        surface->appearanceFlags |= Surface::SeparateSpecularMap;
    if (applyOverlay)
        surface->appearanceFlags |= Surface::ApplyOverlay;
    if (surface->specularColor != Color(0.0f, 0.0f, 0.0f))
        surface->appearanceFlags |= Surface::SpecularReflection;

    if (applyBaseTexture)
        surface->baseTexture.setTexture(baseTexture, path, baseFlags);
    if (applyNightMap)
        surface->nightTexture.setTexture(nightTexture, path, nightFlags);
    if (separateSpecular)
        surface->specularTexture.setTexture(specularTexture, path, specularFlags);

    // If both are present, NormalMap overrides BumpMap
    if (applyNormalMap)
        surface->bumpTexture.setTexture(normalTexture, path, bumpFlags);
    else if (applyBumpMap)
        surface->bumpTexture.setTexture(bumpTexture, path, bumpHeight, bumpFlags);

    if (applyOverlay)
        surface->overlayTexture.setTexture(overlayTexture, path, baseFlags);
}


// Set up the orbit barycenter for a body. By default, it is the parent of the
// object
static Selection GetOrbitBarycenter(const string& name,
                                    PlanetarySystem* system)
{
    Selection orbitBarycenter;
    Body* primary = system->getPrimaryBody();
    if (primary != NULL)
        orbitBarycenter = Selection(primary);
    else
        orbitBarycenter = Selection(system->getStar());

    // The barycenter must be in the same star system as the object we're creating
    if (orbitBarycenter.body())
    {
        if (system->getStar() != orbitBarycenter.body()->getSystem()->getStar())
        {
            cerr << "OrbitBarycenter" << _(" of ") << name << _(" must be in same star system\n");
            return Selection();
        }
    }
    else if (orbitBarycenter.star())
    {
        if (system->getStar() != orbitBarycenter.star())
        {
            cerr << "OrbitBarycenter" << _(" of ") << name << _(" must be in same star system\n");
            return Selection();
        }
    }

    return orbitBarycenter;
}


TimelinePhase* CreateTimelinePhase(Body* body,
                                   Universe& universe,
                                   Hash* phaseData,
                                   const string& path,
                                   ReferenceFrame* defaultFrame,
                                   bool isFirstPhase,
                                   bool isLastPhase,
                                   double previousPhaseEnd)
{
    double beginning = previousPhaseEnd;
    double ending = numeric_limits<double>::infinity();

    // Beginning is optional for the first phase of a timeline, and not
    // allowed for the other phases, where beginning is always the ending
    // of the previous phase.
    bool hasBeginning = ParseDate(phaseData, "Beginning", beginning);
    if (!isFirstPhase && hasBeginning)
    {
        clog << "Error: Beginning can only be specified for initial phase of timeline.\n";
        return NULL;
    }

    // Ending is required for all phases except for the final one.
    bool hasEnding = ParseDate(phaseData, "Ending", ending);
    if (!isLastPhase && !hasEnding)
    {
        clog << "Error: Ending is required for all timeline phases other than the final one.\n";
        return NULL;
    }

    // Get the orbit reference frame.
    ReferenceFrame* orbitFrame;
    Value* frameValue = phaseData->getValue("OrbitFrame");
    if (frameValue != NULL)
    {
        orbitFrame = CreateReferenceFrame(universe, frameValue);
        if (orbitFrame == NULL)
        {
            return NULL;
        }
    }
    else
    {
        // No orbit frame specified; use the default frame.
        orbitFrame = defaultFrame;
        orbitFrame->addRef();
    }

    // Get the body reference frame
    ReferenceFrame* bodyFrame;
    Value* bodyFrameValue = phaseData->getValue("BodyFrame");
    if (bodyFrameValue != NULL)
    {
        bodyFrame = CreateReferenceFrame(universe, bodyFrameValue);
        if (bodyFrame == NULL)
        {
            orbitFrame->release();
            return NULL;
        }
    }
    else
    {
        // No body frame specified; use the default frame.
        bodyFrame = defaultFrame;
        bodyFrame->addRef();
    }

    // Use planet units (AU for semimajor axis) if the center of the orbit 
    // reference frame is a star.
    bool usePlanetUnits = orbitFrame->getCenter().star() != NULL;

    // Get the orbit
    Orbit* orbit = CreateOrbit(NULL, phaseData, path, usePlanetUnits);
    if (!orbit)
    {
        clog << "Error: missing orbit in timeline phase.\n";
        bodyFrame->release();
        orbitFrame->release();
        return NULL;
    }

    // Get the rotation model
    // TIMELINE-TODO: default rotation model is UniformRotation with a period
    // equal to the orbital period. Should we do something else?
    RotationModel* rotationModel = CreateRotationModel(phaseData, path, orbit->getPeriod());
    if (!rotationModel)
    {
        // TODO: Should distinguish between a missing rotation model (where it's
        // appropriate to use a default one) and a bad rotation model (where
        // we should report an error.)
        rotationModel = new ConstantOrientation(Quatd(1.0));
    }

    return TimelinePhase::CreateTimelinePhase(universe,
                                              body,
                                              beginning, ending,
                                              *orbitFrame,
                                              *orbit,
                                              *bodyFrame,
                                              *rotationModel);
}


Timeline* CreateTimelineFromArray(Body* body,
                                  Universe& universe,
                                  Array* timelineArray,
                                  const string& path,
                                  ReferenceFrame* defaultFrame)
{
    Timeline* timeline = new Timeline();
    double previousEnding = -numeric_limits<double>::infinity();

    for (Array::const_iterator iter = timelineArray->begin(); iter != timelineArray->end(); iter++)
    {
        Hash* phaseData = (*iter)->getHash();
        if (phaseData == NULL)
        {
            clog << "Error: Timeline phase " << iter - timelineArray->begin() + 1 << " is not a property group.\n";
            delete timeline;
            return NULL;
        }

        bool isFirstPhase = iter == timelineArray->begin();
        bool isLastPhase = *iter == timelineArray->back();

        TimelinePhase* phase = CreateTimelinePhase(body, universe, phaseData,
                                                   path,
                                                   defaultFrame,
                                                   isFirstPhase, isLastPhase, previousEnding);
        if (phase == NULL)
        {
            clog << "Error in timeline phase " << iter - timelineArray->begin() + 1 << endl;
            delete timeline;
            return NULL;
        }

        previousEnding = phase->endTime();

        timeline->appendPhase(phase);
    }

    return timeline;
}


static bool CreateTimeline(Body* body,
                           const string& name,
                           PlanetarySystem* system,
                           Universe& universe,
                           Hash* planetData,
                           const string& path,
                           Disposition disposition)
{
    FrameTree* parentFrameTree = NULL;
    Selection orbitBarycenter = GetOrbitBarycenter(name, system);
    bool orbitsPlanet = false;
    if (orbitBarycenter.body())
    {
        parentFrameTree = orbitBarycenter.body()->getOrCreateFrameTree();
        orbitsPlanet = true;
    }
    else if (orbitBarycenter.star())
    {
        SolarSystem* solarSystem = universe.getSolarSystem(orbitBarycenter.star());
        if (solarSystem == NULL)
            solarSystem = universe.createSolarSystem(orbitBarycenter.star());
        parentFrameTree = solarSystem->getFrameTree();
    }
    else
    {
        // Bad orbit barycenter specified
        return false;
    }

    // If there's an explicit timeline definition, parse that. Otherwise, we'll do
    // things the old way.
    Value* value = planetData->getValue("Timeline");
    if (value != NULL)
    {
        if (value->getType() != Value::ArrayType)
        {
            clog << "Error: Timeline must be an array\n";
            return false;
        }

        Timeline* timeline = CreateTimelineFromArray(body, universe, value->getArray(), path,
                                                     parentFrameTree->getDefaultReferenceFrame());
        if (timeline == NULL)
        {
            return false;
        }
        else
        {
            body->setTimeline(timeline);
            return true;
        }
    }

    // Information required for the object timeline.
    ReferenceFrame* orbitFrame   = NULL;
    ReferenceFrame* bodyFrame    = NULL;
    Orbit* orbit                 = NULL;
    RotationModel* rotationModel = NULL;
    double beginning             = -numeric_limits<double>::infinity();
    double ending                =  numeric_limits<double>::infinity();

    // If any new timeline values are specified, we need to overrideOldTimeline will
    // be set to true.
    bool overrideOldTimeline = false;

    // The interaction of Modify with timelines is slightly complicated. If the timeline
    // is specified by putting the OrbitFrame, Orbit, BodyFrame, or RotationModel directly
    // in the object definition (i.e. not inside a Timeline structure), it will completely
    // replace the previous timeline if it contained more than one phase. Otherwise, the
    // properties of the single phase will be modified individually, for compatibility with
    // Celestia versions 1.5.0 and earlier.
    if (disposition == ModifyObject)
    {
        const Timeline* timeline = body->getTimeline();
        if (timeline->phaseCount() == 1)
        {
            const TimelinePhase* phase = timeline->getPhase(0);
            orbitFrame    = phase->orbitFrame();
            bodyFrame     = phase->bodyFrame();
            orbit         = phase->orbit();
            rotationModel = phase->rotationModel();
            beginning     = phase->startTime();
            ending        = phase->endTime();
        }
    }

    // Get the object's orbit reference frame.
    bool newOrbitFrame = false;
    Value* frameValue = planetData->getValue("OrbitFrame");
    if (frameValue != NULL)
    {
        ReferenceFrame* frame = CreateReferenceFrame(universe, frameValue);
        if (frame != NULL)
        {
            orbitFrame = frame;
            newOrbitFrame = true;
            overrideOldTimeline = true;
        }
    }

    // Get the object's body frame.
    bool newBodyFrame = false;
    Value* bodyFrameValue = planetData->getValue("BodyFrame");
    if (bodyFrameValue != NULL)
    {
        ReferenceFrame* frame = CreateReferenceFrame(universe, bodyFrameValue);
        if (frame != NULL)
        {
            bodyFrame = frame;
            newBodyFrame = true;
            overrideOldTimeline = true;
        }
    }

    // If no orbit or body frame was specified, use the default ones
    if (orbitFrame == NULL)
        orbitFrame = parentFrameTree->getDefaultReferenceFrame();
    if (bodyFrame == NULL)
        bodyFrame = parentFrameTree->getDefaultReferenceFrame();

    // If the center of the is a star, orbital element units are
    // in AU; otherwise, use kilometers.
    if (orbitFrame->getCenter().star() != NULL)
        orbitsPlanet = false;
    else
        orbitsPlanet = true;

    Orbit* newOrbit = CreateOrbit(system, planetData, path, !orbitsPlanet);
    if (newOrbit == NULL && orbit == NULL)
    {
        clog << "No valid orbit specified for object '" << body->getName() << "'. Skipping.";
        return false;
    }

    // If a new orbit was given, override any old orbit
    if (newOrbit != NULL)
    {
        orbit = newOrbit;
        overrideOldTimeline = true;
    }

    // Get the rotation model for this body
    double syncRotationPeriod = orbit->getPeriod();
    RotationModel* newRotationModel = CreateRotationModel(planetData, path, syncRotationPeriod);

    // If a new rotation model was given, override the old one
    if (newRotationModel != NULL)
    {
        rotationModel = newRotationModel;
        overrideOldTimeline = true;
    }

    // If there was no rotation model specified, nor a previous rotation model to
    // override, create the default one.
    if (rotationModel == NULL)
    {
        // If no rotation model is provided, use a default rotation model--
        // a uniform rotation that's synchronous with the orbit (appropriate
        // for nearly all natural satellites in the solar system.)
        rotationModel = CreateDefaultRotationModel(syncRotationPeriod);
    }
    
    if (ParseDate(planetData, "Beginning", beginning) ||
        ParseDate(planetData, "Ending", ending))
    {
        overrideOldTimeline = true;
    }

    // Something went wrong if the disposition isn't modify and no timeline
    // is to be created.
    assert(disposition == ModifyObject || overrideOldTimeline);

    if (overrideOldTimeline)
    {
        // We finally have an orbit, rotation model, frames, and time range. Create
        // the object timeline.
        TimelinePhase* phase = TimelinePhase::CreateTimelinePhase(universe,
                                                                  body,
                                                                  beginning, ending,
                                                                  *orbitFrame,
                                                                  *orbit,
                                                                  *bodyFrame,
                                                                  *rotationModel);
        Timeline* timeline = new Timeline();
        timeline->appendPhase(phase);

        body->setTimeline(timeline);

        // Check for circular references in frames; this can only be done once the timeline
        // has actually been set.
        // TIMELINE-TODO: This check is not comprehensive; it won't find recursion in
        // multiphase timelines.
        if (newOrbitFrame && isFrameCircular(*body->getOrbitFrame(0.0), ReferenceFrame::PositionFrame))
        {
            clog << "Orbit frame for " << body->getName() << " is nested too deep (probably circular)\n";
            return false;
        }

        if (newBodyFrame && isFrameCircular(*body->getBodyFrame(0.0), ReferenceFrame::OrientationFrame))
        {
            clog << "Body frame for " << body->getName() << " is nested too deep (probably circular)\n";
            return false;
        }
    }

    return true;
}


// Create a body (planet or moon) using the values from a hash
// The usePlanetsUnits flags specifies whether period and semi-major axis
// are in years and AU rather than days and kilometers
static Body* CreatePlanet(const string& name,
                          PlanetarySystem* system,
                          Universe& universe,
                          Body* existingBody,
                          Hash* planetData,
                          const string& path,
                          Disposition disposition)
{
    Body* body = NULL;

    if (disposition == ModifyObject)
    {
        body = existingBody;
    }

    if (body == NULL)
    {
        body = new Body(system);
    }

    if (!CreateTimeline(body, name, system, universe, planetData, path, disposition))
    {
        // No valid timeline given; give up.
        if (body != existingBody)
            delete body;
        return NULL;
    }

    // Three values control the shape and size of an ellipsoidal object:
    // semiAxes, radius, and oblateness. It is an error if neither the
    // radius nor semiaxes are set. If both are set, the radius is
    // multipled by each of the specified semiaxis to give the shape of
    // the body ellipsoid. Oblateness is ignored if semiaxes are provided;
    // otherwise, the ellipsoid has semiaxes: ( radius, radius, 1-radius ).
    // These rather complex rules exist to maintain backward compatibility.
    //
    // If the body also has a mesh, it is always scaled in x, y, and z by
    // the maximum semiaxis, never anisotropically.

    double radius = (double) body->getRadius();
    bool radiusSpecified = false;
    if (planetData->getNumber("Radius", radius))
    {
        body->setSemiAxes(Vec3f((float) radius, (float) radius, (float) radius));
        radiusSpecified = true;
    }

    Vec3d semiAxes;
    if (planetData->getVector("SemiAxes", semiAxes))
    {
        if (radiusSpecified)
            semiAxes *= radius;
        // Swap y and z to match internal coordinate system
        body->setSemiAxes(Vec3f((float) semiAxes.x, (float) semiAxes.z, (float) semiAxes.y));
    }
    else
    {
        double oblateness = 0.0;
        if (planetData->getNumber("Oblateness", oblateness))
        {
            body->setSemiAxes((float) body->getRadius() * Vec3f(1.0f, 1.0f - (float) oblateness, 1.0f));
        }
    }


    int classification = body->getClassification();
    string classificationName;
    if (planetData->getString("Class", classificationName))
    {
        if (compareIgnoringCase(classificationName, "planet") == 0)
            classification = Body::Planet;
        else if (compareIgnoringCase(classificationName, "moon") == 0)
            classification = Body::Moon;
        else if (compareIgnoringCase(classificationName, "comet") == 0)
            classification = Body::Comet;
        else if (compareIgnoringCase(classificationName, "asteroid") == 0)
            classification = Body::Asteroid;
        else if (compareIgnoringCase(classificationName, "spacecraft") == 0)
            classification = Body::Spacecraft;
        else if (compareIgnoringCase(classificationName, "invisible") == 0)
            classification = Body::Invisible;
        else if (compareIgnoringCase(classificationName, "surfacefeature") == 0)
            classification = Body::SurfaceFeature;
        else if (compareIgnoringCase(classificationName, "component") == 0)
            classification = Body::Component;
    }

    if (classification == Body::Unknown)
    {
        // Try to guess the type
        if (system->getPrimaryBody() != NULL)
        {
            if(radius > 0.1)
                classification = Body::Moon;
            else
                classification = Body::Spacecraft;
        }
        else
        {
            if (radius < 1000.0)
                classification = Body::Asteroid;
            else
                classification = Body::Planet;
        }
    }
    body->setClassification(classification);

    if (classification == Body::Invisible)
        body->setVisible(false);

    // Surface features and component objects are by default not
    // visible as points at a distance.
    if (classification == Body::Invisible ||
        classification == Body::SurfaceFeature ||
        classification == Body::Component)
    {
        body->setVisibleAsPoint(false);
    }

    string infoURL;
    if (planetData->getString("InfoURL", infoURL))
    {
        if (infoURL.find(':') == string::npos)
        {
            // Relative URL, the base directory is the current one,
            // not the main installation directory
            if (path[1] == ':')
                // Absolute Windows path, file:/// is required
                infoURL = "file:///" + path + "/" + infoURL;
            else if (!path.empty())
                infoURL = path + "/" + infoURL;
        }
        body->setInfoURL(infoURL);
    }

    double albedo = 0.5;
    if (planetData->getNumber("Albedo", albedo))
        body->setAlbedo((float) albedo);

    double mass = 0.0;
    if (planetData->getNumber("Mass", mass))
        body->setMass((float) mass);

    Quatf orientation;
    if (planetData->getRotation("Orientation", orientation))
        body->setOrientation(orientation);

    Surface surface;
    if (disposition == ModifyObject)
    {
        surface = body->getSurface();
    }
    else
    {
        surface.color = Color(1.0f, 1.0f, 1.0f);
        surface.hazeColor = Color(0.0f, 0.0f, 0.0f, 0.0f);
    }
    FillinSurface(planetData, &surface, path);
    body->setSurface(surface);

    {
        string model("");
        if (planetData->getString("Mesh", model))
        {
            Vec3f modelCenter(0.0f, 0.0f, 0.0f);
            if (planetData->getVector("MeshCenter", modelCenter))
            {
                // TODO: Adjust bounding radius if model center isn't
                // (0.0f, 0.0f, 0.0f)
            }

            ResourceHandle modelHandle = GetModelManager()->getHandle(ModelInfo(model, path, modelCenter));
            body->setModel(modelHandle);

        }
    }

    // Read the atmosphere
    {
        Value* atmosDataValue = planetData->getValue("Atmosphere");
        if (atmosDataValue != NULL)
        {
            if (atmosDataValue->getType() != Value::HashType)
            {
                cout << "ReadSolarSystem: Atmosphere must be an assoc array.\n";
            }
            else
            {
                Hash* atmosData = atmosDataValue->getHash();
                assert(atmosData != NULL);

                Atmosphere* atmosphere = NULL;
                if (disposition == ModifyObject)
                {
                    atmosphere = body->getAtmosphere();
                    if (atmosphere == NULL)
                    {
                        Atmosphere atm;
                        body->setAtmosphere(atm);
                        atmosphere = body->getAtmosphere();
                    }
                }
                else
                {
                    atmosphere = new Atmosphere();
                }
                atmosData->getNumber("Height", atmosphere->height);
                atmosData->getColor("Lower", atmosphere->lowerColor);
                atmosData->getColor("Upper", atmosphere->upperColor);
                atmosData->getColor("Sky", atmosphere->skyColor);
                atmosData->getColor("Sunset", atmosphere->sunsetColor);

                atmosData->getNumber("Mie", atmosphere->mieCoeff);
                atmosData->getNumber("MieScaleHeight", atmosphere->mieScaleHeight);
                atmosData->getNumber("MieAsymmetry", atmosphere->miePhaseAsymmetry);
                atmosData->getVector("Rayleigh", atmosphere->rayleighCoeff);
                //atmosData->getNumber("RayleighScaleHeight", atmosphere->rayleighScaleHeight);
                atmosData->getVector("Absorption", atmosphere->absorptionCoeff);

                // Get the cloud map settings
                atmosData->getNumber("CloudHeight", atmosphere->cloudHeight);
                if (atmosData->getNumber("CloudSpeed", atmosphere->cloudSpeed))
                    atmosphere->cloudSpeed = degToRad(atmosphere->cloudSpeed);

                string cloudTexture;
                if (atmosData->getString("CloudMap", cloudTexture))
                {
                    atmosphere->cloudTexture.setTexture(cloudTexture,
                                                        path,
                                                        TextureInfo::WrapTexture);
                }

                string cloudNormalMap;
                if (atmosData->getString("CloudNormalMap", cloudNormalMap))
                {
                    atmosphere->cloudNormalMap.setTexture(cloudNormalMap,
                                                           path,
                                                           TextureInfo::WrapTexture);
                }

                body->setAtmosphere(*atmosphere);
                if (disposition != ModifyObject)
                    delete atmosphere;
            }
        }
    }

    // Read the ring system
    {
        Value* ringsDataValue = planetData->getValue("Rings");
        if (ringsDataValue != NULL)
        {
            if (ringsDataValue->getType() != Value::HashType)
            {
                cout << "ReadSolarSystem: Rings must be an assoc array.\n";
            }
            else
            {
                Hash* ringsData = ringsDataValue->getHash();
                // ASSERT(ringsData != NULL);

                RingSystem rings(0.0f, 0.0f);
                if (body->getRings() != NULL)
                    rings = *body->getRings();

                double inner = 0.0, outer = 0.0;
                if (ringsData->getNumber("Inner", inner))
                    rings.innerRadius = (float) inner;
                if (ringsData->getNumber("Outer", outer))
                    rings.outerRadius = (float) outer;

                Color color(1.0f, 1.0f, 1.0f);
                if (ringsData->getColor("Color", color))
                    rings.color = color;

                string textureName;
                if (ringsData->getString("Texture", textureName))
                    rings.texture = MultiResTexture(textureName, path);

                body->setRings(rings);
            }
        }
    }
    
    bool clickable = true;
    if (planetData->getBoolean("Clickable", clickable))
    {
        body->setClickable(clickable);
    }

    bool visible = true;
    if (planetData->getBoolean("Visible", visible))
    {
        body->setVisible(visible);
    }

    Color orbitColor;
    if (planetData->getColor("OrbitColor", orbitColor))
    {
        body->setOrbitColorOverridden(true);
        body->setOrbitColor(orbitColor);
    }

    return body;
}


// Create a barycenter object using the values from a hash
static Body* CreateReferencePoint(const string& name,
                                  PlanetarySystem* system,
                                  Universe& universe,
                                  Body* existingBody,
                                  Hash* refPointData,
                                  const string& path,
                                  Disposition disposition)
{
    Body* body = NULL;

    if (disposition == ModifyObject)
    {
        body = existingBody;
    }

    if (body == NULL)
    {
        body = new Body(system);
    }

    body->setSemiAxes(Vec3f(1.0f, 1.0f, 1.0f));
    body->setClassification(Body::Invisible);
    body->setVisible(false);
    body->setVisibleAsPoint(false);
    body->setClickable(false);

    if (!CreateTimeline(body, name, system, universe, refPointData, path, disposition))
    {
        // No valid timeline given; give up.
        if (body != existingBody)
            delete body;
        return NULL;
    }

    return body;
}


bool LoadSolarSystemObjects(istream& in,
                            Universe& universe,
                            const std::string& directory)
{
    Tokenizer tokenizer(&in);
    Parser parser(&tokenizer);

    while (tokenizer.nextToken() != Tokenizer::TokenEnd)
    {
        // Read the disposition; if none is specified, the default is Add.
        Disposition disposition = AddObject;
        if (tokenizer.getTokenType() == Tokenizer::TokenName)
        {
            if (tokenizer.getNameValue() == "Add")
            {
                disposition = AddObject;
                tokenizer.nextToken();
            }
            else if (tokenizer.getNameValue() == "Replace")
            {
                disposition = ReplaceObject;
                tokenizer.nextToken();
            }
            else if (tokenizer.getNameValue() == "Modify")
            {
                disposition = ModifyObject;
                tokenizer.nextToken();
            }
        }

        // Read the item type; if none is specified the default is Body
        string itemType("Body");
        if (tokenizer.getTokenType() == Tokenizer::TokenName)
        {
            itemType = tokenizer.getNameValue();
            tokenizer.nextToken();
        }

        if (tokenizer.getTokenType() != Tokenizer::TokenString)
        {
            sscError(tokenizer, "object name expected");
            return false;
        }
        string name = tokenizer.getStringValue().c_str();

        if (tokenizer.nextToken() != Tokenizer::TokenString)
        {
            sscError(tokenizer, "bad parent object name");
            return false;
        }
        string parentName = tokenizer.getStringValue().c_str();

        Value* objectDataValue = parser.readValue();
        if (objectDataValue == NULL)
        {
            sscError(tokenizer, "bad object definition");
            return false;
        }

        if (objectDataValue->getType() != Value::HashType)
        {
            sscError(tokenizer, "{ expected");
            delete objectDataValue;
            return false;
        }
        Hash* objectData = objectDataValue->getHash();

        Selection parent = universe.findPath(parentName, NULL, 0);
        PlanetarySystem* parentSystem = NULL;

        if (itemType == "Body" || itemType == "ReferencePoint")
        {
            //bool orbitsPlanet = false;
            if (parent.star() != NULL)
            {
                SolarSystem* solarSystem = universe.getSolarSystem(parent.star());
                if (solarSystem == NULL)
                {
                    // No solar system defined for this star yet, so we need
                    // to create it.
                    solarSystem = universe.createSolarSystem(parent.star());
                }
                parentSystem = solarSystem->getPlanets();
            }
            else if (parent.body() != NULL)
            {
                // Parent is a planet or moon
                parentSystem = parent.body()->getSatellites();
                if (parentSystem == NULL)
                {
                    // If the planet doesn't already have any satellites, we
                    // have to create a new planetary system for it.
                    parentSystem = new PlanetarySystem(parent.body());
                    parent.body()->setSatellites(parentSystem);
                }
                //orbitsPlanet = true;
            }
            else
            {
                errorMessagePrelude(tokenizer);
                cerr << _("parent body '") << parentName << _("' of '") << name << _("' not found.\n");
            }

            if (parentSystem != NULL)
            {
                Body* existingBody = parentSystem->find(name);
                if (existingBody && disposition == AddObject)
                {
                    errorMessagePrelude(tokenizer);
                    cerr << _("warning duplicate definition of ") <<
                        parentName << " " <<  name << '\n';
                }

                Body* body;
                if (itemType == "ReferencePoint")
                    body = CreateReferencePoint(name, parentSystem, universe, existingBody, objectData, directory, disposition);
                else
                    body = CreatePlanet(name, parentSystem, universe, existingBody, objectData, directory, disposition);

                if (body != NULL)
                {
                    body->setName(name);
                    if (disposition == ReplaceObject)
                    {
                        parentSystem->replaceBody(existingBody, body);
                        delete existingBody;
                    }
                    else if (disposition == AddObject)
                    {
                        parentSystem->addBody(body);
                    }
                }
            }
        }
        else if (itemType == "AltSurface")
        {
            Surface* surface = new Surface();
            surface->color = Color(1.0f, 1.0f, 1.0f);
            surface->hazeColor = Color(0.0f, 0.0f, 0.0f, 0.0f);
            FillinSurface(objectData, surface, directory);
            if (surface != NULL && parent.body() != NULL)
                parent.body()->addAlternateSurface(name, surface);
            else
                sscError(tokenizer, _("bad alternate surface"));
        }
        else if (itemType == "Location")
        {
            if (parent.body() != NULL)
            {
                Location* location = CreateLocation(objectData, parent.body());
                if (location != NULL)
                {
                    location->setName(name);
                    parent.body()->addLocation(location);
                }
                else
                {
                    sscError(tokenizer, _("bad location"));
                }
            }
            else
            {
                errorMessagePrelude(tokenizer);
                cerr << _("parent body '") << parentName << _("' of '") << name << _("' not found.\n");
            }
        }
        delete objectDataValue;
    }

    // TODO: Return some notification if there's an error parsing the file
    return true;
}


SolarSystem::SolarSystem(Star* _star) : 
    star(_star),
    planets(NULL),
    frameTree(NULL)
{
    planets = new PlanetarySystem(star);
    frameTree = new FrameTree(star);
}


Star* SolarSystem::getStar() const
{
    return star;
}

Point3f SolarSystem::getCenter() const
{
    // TODO: This is a very simple method at the moment, but it will get
    // more complex when planets around multistar systems are supported
    // where the planets may orbit the center of mass of two stars.
    return star->getPosition();
}

PlanetarySystem* SolarSystem::getPlanets() const
{
    return planets;
}

FrameTree* SolarSystem::getFrameTree() const
{
    return frameTree;
}
